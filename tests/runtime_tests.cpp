// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixes.h"
#include "plugin.h"

#include <array>
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fstream>
#include <inttypes.h>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>

#ifdef NDEBUG
#error Regression tests require assertions.
#endif

using namespace l4fixes;
static_assert(!std::is_copy_constructible_v<CodePatch>);
static_assert(!std::is_move_constructible_v<CodePatch>);

namespace
{
class SharedLibrary
{
public:
	explicit SharedLibrary(const char* path) : m_handle(dlopen(path, RTLD_NOW | RTLD_LOCAL))
	{
		if (m_handle == nullptr)
			throw std::runtime_error(dlerror());
	}
	~SharedLibrary() { dlclose(m_handle); }
	void* Symbol(const char* name) const { return dlsym(m_handle, name); }
private:
	void* m_handle;
};

template <typename Operation>
void ExpectFailure(Operation operation)
{
	bool failed = false;
	try { operation(); }
	catch (const std::exception&) { failed = true; }
	assert(failed);
}

std::vector<Byte> FileBytes(const std::string& path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	assert(file && file.tellg() > 0);
	std::vector<Byte> bytes(static_cast<std::size_t>(file.tellg()));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
	assert(file);
	return bytes;
}

std::string Permissions(std::uintptr_t address)
{
	std::ifstream file("/proc/self/maps");
	std::string line;
	while (std::getline(file, line))
	{
		std::uintptr_t start = 0, end = 0;
		char permissions[5] = {};
		if (std::sscanf(line.c_str(), "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, permissions) == 3
			&& address >= start && address < end)
			return permissions;
	}
	throw std::runtime_error("Test mapping not found");
}

void TestElf(const LoadedModule& module)
{
	auto image = FileBytes(module.Path());
	for (const auto& definition : FixDefinitions())
	{
		const auto symbol = FindElfFunction(image, definition.symbol);
		assert(symbol.size >= definition.offset + definition.replacement.size());
	}
	ExpectFailure([&] { FindElfFunction(image, "missing_function"); });
	ExpectFailure([&] { FindElfFunction(std::span(image).first(10), "missing_function"); });
	image[EI_CLASS] = ELFCLASS64;
	ExpectFailure([&] { FindElfFunction(image, FixDefinitions()[0].symbol); });
	image = FileBytes(module.Path());
	Elf32_Ehdr header;
	std::memcpy(&header, image.data(), sizeof(header));
	header.e_shoff = std::numeric_limits<Elf32_Off>::max();
	std::memcpy(image.data(), &header, sizeof(header));
	ExpectFailure([&] { FindElfFunction(image, FixDefinitions()[0].symbol); });
	ExpectFailure([&] { module.ResolveFunction("missing_function"); });
	assert(!module.ContainsCode(1, 1));
}

void TestFixes(const LoadedModule& module)
{
	const auto count = FixDefinitions().size();
	assert(count == 4);
	std::vector<std::vector<Byte>> originals(count);
	std::vector<std::uintptr_t> addresses(count);
	std::vector<std::string> permissions(count);
	std::size_t index = 0;
	for (const auto& definition : FixDefinitions())
	{
		addresses[index] = module.ResolveFunction(definition.symbol).address + definition.offset;
		originals[index] = ReadMemory(addresses[index], definition.replacement.size());
		permissions[index] = Permissions(addresses[index]);
		++index;
	}
	{
		PatchManager manager;
		RegisterFixes(manager, module, module);
		manager.EnableAll();
		manager.EnableAll();
		index = 0;
		for (const auto& definition : FixDefinitions())
		{
			assert(ReadMemory(addresses[index], definition.replacement.size())
				== std::vector<Byte>(definition.replacement.begin(), definition.replacement.end()));
			assert(Permissions(addresses[index]) == permissions[index]);
			++index;
		}
	} // Manager destruction must restore all owned patches.
	for (index = 0; index < originals.size(); ++index)
	{
		assert(ReadMemory(addresses[index], originals[index].size()) == originals[index]);
		assert(Permissions(addresses[index]) == permissions[index]);
	}

	const auto& hltv = FixDefinitions()[1];
	CodePatch external("external HLTV fix", addresses[1], hltv.verify, hltv.replacement);
	external.Enable();
	{
		PatchManager manager;
		RegisterFixes(manager, module, module);
		manager.EnableAll();
		manager.Clear();
		assert(manager.Empty());
	}
	assert(ReadMemory(addresses[1], hltv.replacement.size()) == std::vector<Byte>(hltv.replacement.begin(), hltv.replacement.end()));
	assert(external.Disable());
	assert(ReadMemory(addresses[1], originals[1].size()) == originals[1]);

	// A failed EnableAll must roll back earlier writes and preserve foreign bytes.
	const std::array<Byte, 4> foreignBytes = { 0x90, 0x90, 0x90, 0x90 };
	PatchManager manager;
	RegisterFixes(manager, module, module);
	CodePatch foreign("foreign edit", addresses[1], hltv.verify, foreignBytes);
	foreign.Enable();
	ExpectFailure([&] { manager.EnableAll(); });
	for (index = 0; index < count; ++index)
	{
		if (index != 1)
			assert(ReadMemory(addresses[index], originals[index].size()) == originals[index]);
	}
	assert(ReadMemory(addresses[1], foreignBytes.size()) == std::vector<Byte>(foreignBytes.begin(), foreignBytes.end()));
	assert(foreign.Disable());
	manager.Clear();
	ExpectFailure([&] { manager.Register(nullptr); });
}

void TestMemory()
{
	const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
	assert(pageSize > 0);
	void* allocation = mmap(nullptr, pageSize * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(allocation != MAP_FAILED);
	auto* memory = static_cast<Byte*>(allocation);
	std::memset(memory, 0xCC, pageSize * 2);
	const auto address = reinterpret_cast<std::uintptr_t>(memory + pageSize - 2);
	const std::array<Byte, 4> original = { 0xCC, 0xCC, 0xCC, 0xCC };
	const std::array<Byte, 4> replacement = { 0x90, 0x90, 0x90, 0x90 };
	assert(mprotect(memory, pageSize * 2, PROT_READ | PROT_EXEC) == 0);
	{
		CodePatch patch("cross-page", address, original, replacement);
		patch.Enable();
		assert(Permissions(address).substr(0, 3) == "r-x");
		assert(Permissions(address + 3).substr(0, 3) == "r-x");
		assert(patch.Disable());
		assert(patch.Disable());
		patch.Enable();
	}
	assert(ReadMemory(address, original.size()) == std::vector<Byte>(original.begin(), original.end()));
	assert(mprotect(memory + pageSize, pageSize, PROT_NONE) == 0);
	ExpectFailure([&] { CodePatch patch("inaccessible", address, original, replacement); });
	ExpectFailure([&] { ReadMemory(1, 1); });
	ExpectFailure([&] { ReadMemory(std::numeric_limits<std::uintptr_t>::max(), 4); });
	assert(munmap(memory, pageSize * 2) == 0);
}

void TestPluginInterface(const char* path)
{
	SharedLibrary plugin(path);
	using Factory = void* (*)(const char*, int*);
	const auto factory = reinterpret_cast<Factory>(plugin.Symbol("CreateInterface"));
	assert(factory != nullptr);
	int result = IFACE_OK;
	assert(factory("unknown", &result) == nullptr && result == IFACE_FAILED);
	assert(factory(nullptr, nullptr) == nullptr);
	auto* callbacks = static_cast<IServerPluginCallbacks*>(factory(INTERFACEVERSION_ISERVERPLUGINCALLBACKS, &result));
	assert(callbacks != nullptr && result == IFACE_OK);
	assert(std::strstr(callbacks->GetPluginDescription(), "L4FixesVSP 1.0.0") != nullptr);
	assert(!callbacks->Load(nullptr, nullptr));
	callbacks->Unload();
}
}

void TestReliableSound(); // reliablesound_tests.cpp

int main(int argumentCount, char** arguments)
{
	assert(argumentCount == 3);
	SharedLibrary fixture(arguments[1]);
	assert(fixture.Symbol("FixtureAnchor") != nullptr);
	assert(fixture.Symbol(FixDefinitions()[0].symbol.data()) == nullptr);
	LoadedModule module(fixture.Symbol("FixtureAnchor"));
	TestElf(module);
	TestFixes(module);
	TestMemory();
	TestReliableSound();
	TestPluginInterface(arguments[2]);
	std::puts("All L4FixesVSP regression tests passed.");
}
