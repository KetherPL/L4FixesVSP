// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin.h"
#include "fixes.h"

#include <cstdio>
#include <cstring>
#include <limits.h>
#include <stdexcept>

#if !defined(__linux__) || !defined(__i386__)
#error L4FixesVSP supports Linux i386 L4D2 servers only.
#endif
static_assert(sizeof(void*) == 4, "Patch definitions use 32-bit x86 instructions");

namespace l4fixes
{
namespace
{
L4FixesVSP g_plugin;

// Interfaces may be allocated on the heap; their first virtual method belongs to
// the implementing library even when a factory has been wrapped by another plugin.
const void* ModuleAnchor(const void* interface)
{
	const void* const* vtable;
	std::memcpy(&vtable, interface, sizeof(vtable));
	return vtable[0];
}
}

bool L4FixesVSP::Load(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory)
{
	if (m_loaded)
		return true;
	try
	{
		if (engineFactory == nullptr || serverFactory == nullptr)
			throw std::runtime_error("Missing engine/game interface factory");
		auto* engine = static_cast<IVEngineServer*>(engineFactory(INTERFACEVERSION_VENGINESERVER, nullptr));
		auto* server = static_cast<IServerGameDLL*>(serverFactory(INTERFACEVERSION_SERVERGAMEDLL, nullptr));
		if (engine == nullptr || server == nullptr)
			throw std::runtime_error("Missing L4D2 engine/game interface");
		char gameDirectory[PATH_MAX] = {};
		engine->GetGameDir(gameDirectory, sizeof(gameDirectory));
		gameDirectory[sizeof(gameDirectory) - 1] = '\0';
		std::string_view game(gameDirectory);
		while (game.ends_with('/'))
			game.remove_suffix(1);
		game.remove_prefix(game.find_last_of('/') == std::string_view::npos ? 0 : game.find_last_of('/') + 1);
		if (game != "left4dead2")
			throw std::runtime_error("Only Left 4 Dead 2 is supported");

		const LoadedModule serverModule(ModuleAnchor(server));
		const LoadedModule engineModule(ModuleAnchor(engine));
		std::fprintf(stdout, "[L4FixesVSP] Server: %s\n[L4FixesVSP] Engine: %s\n",
			serverModule.Path().c_str(), engineModule.Path().c_str());
		// Resolve and validate every fix before writing any code.
		RegisterFixes(m_patches, serverModule, engineModule);
		m_patches.EnableAll();
		m_loaded = true;
		std::fprintf(stdout, "[L4FixesVSP] Version %s loaded; all three fixes are present.\n", VERSION);
		return true;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "[L4FixesVSP] Load failed: %s\n", error.what());
		m_patches.Clear();
		return false;
	}
}

void L4FixesVSP::Unload()
{
	m_patches.Clear();
	m_loaded = false;
}

const char* L4FixesVSP::GetPluginDescription()
{
	static const std::string description = std::string("L4FixesVSP ") + VERSION + " - native L4D2 engine/game fixes";
	return description.c_str();
}
}

// Avoid SDK registration helpers and their tier1 runtime dependency. The engine
// discovers this one C export and negotiates the SDK's exact callback interface.
extern "C" __attribute__((visibility("default"))) void* CreateInterface(const char* name, int* returnCode)
{
	if (name != nullptr && std::strcmp(name, INTERFACEVERSION_ISERVERPLUGINCALLBACKS) == 0)
	{
		if (returnCode != nullptr)
			*returnCode = IFACE_OK;
		return &l4fixes::g_plugin;
	}
	if (returnCode != nullptr)
		*returnCode = IFACE_FAILED;
	return nullptr;
}
