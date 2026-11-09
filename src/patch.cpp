// SPDX-License-Identifier: GPL-3.0-or-later
#include "patch.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <inttypes.h>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

namespace l4fixes
{
namespace
{
struct ProtectedRange
{
	std::uintptr_t address;
	std::size_t length;
	int protection;
};

std::runtime_error SystemError(const char* operation)
{
	return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

void CheckRange(std::uintptr_t address, std::size_t length)
{
	if (address == 0 || length == 0 || length > std::numeric_limits<std::uintptr_t>::max() - address)
		throw std::runtime_error("Invalid memory range");
}

std::vector<ProtectedRange> GetCodePages(std::uintptr_t address, std::size_t length)
{
	CheckRange(address, length);
	const long pageSizeResult = sysconf(_SC_PAGESIZE);
	if (pageSizeResult <= 0)
		throw std::runtime_error("Cannot determine Linux page size");
	const auto pageSize = static_cast<std::size_t>(pageSizeResult);
	const auto startPage = address - address % pageSize;
	const auto endAddress = address + length;
	if (endAddress > std::numeric_limits<std::uintptr_t>::max() - (pageSize - 1))
		throw std::runtime_error("Page boundary overflow");
	const auto endPage = ((endAddress + pageSize - 1) / pageSize) * pageSize;
	std::ifstream maps("/proc/self/maps");
	if (!maps)
		throw std::runtime_error("Cannot read /proc/self/maps");
	std::vector<ProtectedRange> ranges;
	std::uintptr_t cursor = startPage;
	std::string line;
	while (cursor < endPage && std::getline(maps, line))
	{
		std::uintptr_t mappingStart = 0;
		std::uintptr_t mappingEnd = 0;
		char permissions[5] = {};
		if (std::sscanf(line.c_str(), "%" SCNxPTR "-%" SCNxPTR " %4s", &mappingStart, &mappingEnd, permissions) != 3)
			throw std::runtime_error("Invalid /proc/self/maps entry");
		if (mappingEnd <= cursor)
			continue;
		if (mappingStart > cursor || permissions[0] != 'r' || permissions[2] != 'x')
			throw std::runtime_error("Patch target is not fully mapped readable executable memory");
		const int protection = PROT_READ | PROT_EXEC | (permissions[1] == 'w' ? PROT_WRITE : 0);
		const auto rangeEnd = std::min(mappingEnd, endPage);
		ranges.push_back({ cursor, rangeEnd - cursor, protection });
		cursor = rangeEnd;
	}
	if (cursor != endPage)
		throw std::runtime_error("Patch target crosses unmapped memory");
	return ranges;
}

bool RestoreProtection(std::span<const ProtectedRange> ranges) noexcept
{
	bool restored = true;
	for (const auto& range : ranges)
	{
		if (mprotect(reinterpret_cast<void*>(range.address), range.length, range.protection) != 0)
		{
			std::fprintf(stderr, "[L4FixesVSP] Cannot restore page permissions: %s\n", std::strerror(errno));
			restored = false;
		}
	}
	return restored;
}

void WriteCode(std::uintptr_t address, std::span<const Byte> expected, std::span<const Byte> replacement)
{
	if (expected.size() != replacement.size() || ReadMemory(address, expected.size()) != std::vector<Byte>(expected.begin(), expected.end()))
		throw std::runtime_error("Patch bytes changed before writing");
	const auto ranges = GetCodePages(address, replacement.size());
	std::size_t writableCount = 0;
	for (const auto& range : ranges)
	{
		if (mprotect(reinterpret_cast<void*>(range.address), range.length, range.protection | PROT_WRITE) != 0)
		{
			auto error = SystemError("mprotect");
			RestoreProtection(std::span(ranges).first(writableCount));
			throw error;
		}
		++writableCount;
	}
	// No code paths or function pointers are redirected into the plugin's own image.
	// Patch ranges have been checked and made writable before this single copy.
	std::memcpy(reinterpret_cast<void*>(address), replacement.data(), replacement.size());
	__builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address + replacement.size()));
	if (!RestoreProtection(ranges))
		throw std::runtime_error("Failed to restore patch page permissions");
}
}

std::vector<Byte> ReadMemory(std::uintptr_t address, std::size_t length)
{
	CheckRange(address, length);
	std::vector<Byte> result(length);
	iovec local = { result.data(), result.size() };
	iovec remote = { reinterpret_cast<void*>(address), length };
	const auto bytesRead = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
	if (bytesRead < 0)
		throw SystemError("process_vm_readv");
	if (static_cast<std::size_t>(bytesRead) != length)
		throw std::runtime_error("Incomplete memory read");
	return result;
}

CodePatch::CodePatch(std::string name, std::uintptr_t address, std::span<const Byte> verify,
	std::span<const Byte> replacement)
	: m_name(std::move(name)), m_address(address), m_replacement(replacement.begin(), replacement.end())
{
	if (verify.empty() || replacement.empty() || verify.size() > replacement.size())
		throw std::runtime_error("Invalid patch definition: " + m_name);
	m_original = ReadMemory(m_address, replacement.size());
	m_external = m_original == m_replacement;
	if (!m_external && !std::equal(verify.begin(), verify.end(), m_original.begin()))
		throw std::runtime_error("Unexpected original bytes for " + m_name);
}

CodePatch::~CodePatch()
{
	Disable();
}

void CodePatch::Enable()
{
	if (m_enabled)
		return;
	if (m_external)
	{
		if (ReadMemory(m_address, m_replacement.size()) != m_replacement)
			throw std::runtime_error("External patch changed: " + m_name);
		return;
	}
	// Mark ownership first so an error after the write also triggers rollback.
	m_enabled = true;
	try
	{
		WriteCode(m_address, m_original, m_replacement);
	}
	catch (...)
	{
		Disable();
		throw;
	}
}

bool CodePatch::Disable() noexcept
{
	if (!m_enabled)
		return true;
	try
	{
		const auto current = ReadMemory(m_address, m_replacement.size());
		if (current != m_original)
		{
			if (current != m_replacement)
				throw std::runtime_error("Refusing to overwrite bytes changed by another owner");
			WriteCode(m_address, m_replacement, m_original);
		}
		m_enabled = false;
		return true;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "[L4FixesVSP] Restore failed for %s: %s\n", m_name.c_str(), error.what());
		return false;
	}
}

void PatchManager::Register(std::unique_ptr<CodePatch> patch)
{
	if (!patch)
		throw std::runtime_error("Cannot register a null patch");
	m_patches.push_back(std::move(patch));
}

void PatchManager::EnableAll()
{
	try
	{
		for (auto& patch : m_patches)
		{
			patch->Enable();
			std::fprintf(stdout, "[L4FixesVSP] %s: %s\n", patch->Name().c_str(),
				patch->IsExternal() ? "already installed by another owner" : "enabled");
		}
	}
	catch (...)
	{
		DisableAll();
		throw;
	}
}

bool PatchManager::DisableAll() noexcept
{
	bool restored = true;
	for (auto patch = m_patches.rbegin(); patch != m_patches.rend(); ++patch)
		restored = (*patch)->Disable() && restored;
	return restored;
}

void PatchManager::Clear() noexcept
{
	DisableAll();
	m_patches.clear();
}
}
