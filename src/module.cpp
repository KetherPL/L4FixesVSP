// SPDX-License-Identifier: GPL-3.0-or-later
#include "module.h"

#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fstream>
#include <limits>
#include <link.h>
#include <stdexcept>

namespace l4fixes
{
namespace
{
constexpr std::size_t MAX_MODULE_FILE_SIZE = 256 * 1024 * 1024;

bool Contains(std::size_t total, std::size_t offset, std::size_t length)
{
	return offset <= total && length <= total - offset;
}

template <typename Value>
Value ReadObject(std::span<const Byte> image, std::size_t offset)
{
	if (!Contains(image.size(), offset, sizeof(Value)))
		throw std::runtime_error("Truncated ELF structure");
	Value value;
	std::memcpy(&value, image.data() + offset, sizeof(value));
	return value;
}
}

FunctionSymbol FindElfFunction(std::span<const Byte> image, std::string_view name)
{
	const auto header = ReadObject<Elf32_Ehdr>(image, 0);
	if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0
		|| header.e_ident[EI_CLASS] != ELFCLASS32 || header.e_ident[EI_DATA] != ELFDATA2LSB
		|| header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT
		|| header.e_machine != EM_386 || header.e_type != ET_DYN
		|| header.e_ehsize != sizeof(Elf32_Ehdr) || header.e_shentsize != sizeof(Elf32_Shdr)
		|| header.e_shnum == 0
		|| !Contains(image.size(), header.e_shoff, header.e_shnum * sizeof(Elf32_Shdr)))
	{
		throw std::runtime_error("Expected a complete Linux i386 shared-library ELF image");
	}
	auto sectionAt = [&](std::size_t index) {
		return ReadObject<Elf32_Shdr>(image, header.e_shoff + index * sizeof(Elf32_Shdr));
	};
	FunctionSymbol found = {};
	for (std::size_t sectionIndex = 0; sectionIndex < header.e_shnum; ++sectionIndex)
	{
		const auto symbols = sectionAt(sectionIndex);
		if (symbols.sh_type != SHT_SYMTAB && symbols.sh_type != SHT_DYNSYM)
			continue;
		if (symbols.sh_link >= header.e_shnum || symbols.sh_entsize != sizeof(Elf32_Sym)
			|| symbols.sh_size % sizeof(Elf32_Sym) != 0
			|| !Contains(image.size(), symbols.sh_offset, symbols.sh_size))
			throw std::runtime_error("Invalid ELF symbol table");
		const auto strings = sectionAt(symbols.sh_link);
		if (strings.sh_type != SHT_STRTAB || !Contains(image.size(), strings.sh_offset, strings.sh_size))
			throw std::runtime_error("Invalid ELF string table");
		for (std::size_t offset = 0; offset < symbols.sh_size; offset += sizeof(Elf32_Sym))
		{
			const auto symbol = ReadObject<Elf32_Sym>(image, symbols.sh_offset + offset);
			if (ELF32_ST_TYPE(symbol.st_info) != STT_FUNC || symbol.st_shndx == SHN_UNDEF
				|| symbol.st_name >= strings.sh_size)
				continue;
			const char* symbolName = reinterpret_cast<const char*>(image.data() + strings.sh_offset + symbol.st_name);
			const void* terminator = std::memchr(symbolName, '\0', strings.sh_size - symbol.st_name);
			if (terminator == nullptr)
				throw std::runtime_error("Unterminated ELF symbol name");
			if (std::string_view(symbolName, static_cast<const char*>(terminator) - symbolName) != name)
				continue;
			if (symbol.st_shndx >= header.e_shnum || symbol.st_size == 0)
				throw std::runtime_error("Function has no bounded ELF section: " + std::string(name));
			const auto code = sectionAt(symbol.st_shndx);
			if ((code.sh_flags & SHF_EXECINSTR) == 0 || symbol.st_value < code.sh_addr
				|| !Contains(code.sh_size, symbol.st_value - code.sh_addr, symbol.st_size))
				throw std::runtime_error("Function lies outside its executable ELF section");
			if (found.size != 0 && (found.address != symbol.st_value || found.size != symbol.st_size))
				throw std::runtime_error("Ambiguous ELF function: " + std::string(name));
			found = { symbol.st_value, symbol.st_size };
		}
	}
	if (found.size == 0)
		throw std::runtime_error("Missing function symbol: " + std::string(name));
	return found;
}

LoadedModule::LoadedModule(const void* anchor)
{
	Dl_info information = {};
	if (anchor == nullptr || dladdr(anchor, &information) == 0
		|| information.dli_fbase == nullptr || information.dli_fname == nullptr)
		throw std::runtime_error("Cannot identify a loaded engine/game library");
	m_baseAddress = reinterpret_cast<std::uintptr_t>(information.dli_fbase);
	m_path = information.dli_fname;
	struct ModuleHeaders
	{
		std::uintptr_t baseAddress;
		const ElfW(Phdr)* headers = nullptr;
		std::size_t count = 0;
	} headers = { m_baseAddress };
	// Do not allocate or throw while the loader holds its module-list lock.
	dl_iterate_phdr([](dl_phdr_info* information, std::size_t, void* context) noexcept {
		auto& headers = *static_cast<ModuleHeaders*>(context);
		if (information->dlpi_addr != headers.baseAddress)
			return 0;
		headers.headers = information->dlpi_phdr;
		headers.count = information->dlpi_phnum;
		return 1;
	}, &headers);
	for (std::size_t index = 0; index < headers.count; ++index)
	{
		const auto& segment = headers.headers[index];
		if (segment.p_type == PT_LOAD && (segment.p_flags & (PF_R | PF_X)) == (PF_R | PF_X)
			&& segment.p_vaddr <= std::numeric_limits<std::uintptr_t>::max() - m_baseAddress)
			m_codeSegments.push_back({ m_baseAddress + segment.p_vaddr, segment.p_memsz });
	}
	if (m_codeSegments.empty())
		throw std::runtime_error("Library has no readable executable segment: " + m_path);
	std::ifstream file(m_path, std::ios::binary | std::ios::ate);
	const auto length = file.tellg();
	if (!file || length <= 0 || length > static_cast<std::streamoff>(MAX_MODULE_FILE_SIZE))
		throw std::runtime_error("Cannot read library ELF image: " + m_path);
	m_image.resize(static_cast<std::size_t>(length));
	file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(m_image.data()), static_cast<std::streamsize>(m_image.size())))
		throw std::runtime_error("Incomplete library ELF image: " + m_path);
}

bool LoadedModule::ContainsCode(std::uintptr_t address, std::size_t length) const noexcept
{
	for (const auto& segment : m_codeSegments)
	{
		if (address >= segment.address && Contains(segment.length, address - segment.address, length))
			return true;
	}
	return false;
}

FunctionSymbol LoadedModule::ResolveFunction(std::string_view name) const
{
	auto symbol = FindElfFunction(m_image, name);
	if (symbol.address > std::numeric_limits<std::uintptr_t>::max() - m_baseAddress)
		throw std::runtime_error("Function address overflow");
	symbol.address += m_baseAddress;
	if (!ContainsCode(symbol.address, symbol.size))
		throw std::runtime_error("Function is outside loaded executable segments: " + std::string(name));
	return symbol;
}
}
