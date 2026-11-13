// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin.h"
#include "fixes.h"
#include "reliablesound.h"

#include "edict.h"
#include "engine/IEngineSound.h"
#include "game/server/iplayerinfo.h"
#include "icvar.h"
#include "tier1/convar.h"

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

// Interfaces used by te_reliable_sound; set while the command is registered.
IVEngineServer* g_engine = nullptr;
IEngineSound* g_engineSound = nullptr;
IPlayerInfoManager* g_playerInfoManager = nullptr;
CGlobalVars* g_globals = nullptr;

// Interfaces may be allocated on the heap; their first virtual method belongs to
// the implementing library even when a factory has been wrapped by another plugin.
const void* ModuleAnchor(const void* interface)
{
	const void* const* vtable;
	std::memcpy(&vtable, interface, sizeof(vtable));
	return vtable[0];
}

// Clients a sound is sent to: connected humans (they have a net channel) and SourceTV (a fake client
// without one, which the engine still forwards sounds to). Bots and free slots are skipped.
bool IsSoundRecipient(int client)
{
	if (g_engine == nullptr || g_globals == nullptr || client < 1 || client > g_globals->maxClients)
		return false;
	if (g_engine->GetPlayerNetInfo(client) != nullptr)
		return true;
	if (g_playerInfoManager == nullptr || g_globals->pEdicts == nullptr)
		return false;
	IPlayerInfo* info = g_playerInfoManager->GetPlayerInfo(&g_globals->pEdicts[client]);
	return info != nullptr && info->IsConnected() && info->IsHLTV();
}

// Server-only (no FCVAR_GAMEDLL): the console, rcon and plugins via ServerCommand can run it.
void ReliableSoundCommand(const CCommand& args)
{
	if (g_engineSound == nullptr || g_globals == nullptr)
	{
		Warning("[L4FixesVSP] %s: the plugin is not loaded.\n", RELIABLE_SOUND_COMMAND);
		return;
	}
	std::string error;
	if (!EmitReliableSound(args, *g_engineSound, g_globals->maxClients, IsSoundRecipient, error))
		Warning("[L4FixesVSP] %s: %s\n", RELIABLE_SOUND_COMMAND, error.c_str());
}

// tier1 links this command into its list when the library is loaded; ConVar_Register hands the
// list to the engine.
ConCommand g_reliableSoundCommand(RELIABLE_SOUND_COMMAND, ReliableSoundCommand,
	"Emit a precached sound reliably: <entity> <channel> <soundlevel> <volume> <pitch> <flags>"
	" <x,y,z|-> \"<sample>\" [client,client,...]");
}

void L4FixesVSP::RegisterCommands(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory, IVEngineServer* engine)
{
	auto* cvar = static_cast<ICvar*>(engineFactory(CVAR_INTERFACE_VERSION, nullptr));
	auto* engineSound = static_cast<IEngineSound*>(engineFactory(IENGINESOUND_SERVER_INTERFACE_VERSION, nullptr));
	auto* playerInfoManager = static_cast<IPlayerInfoManager*>(serverFactory(INTERFACEVERSION_PLAYERINFOMANAGER, nullptr));
	CGlobalVars* globals = playerInfoManager != nullptr ? playerInfoManager->GetGlobalVars() : nullptr;
	if (cvar == nullptr || engineSound == nullptr || playerInfoManager == nullptr || globals == nullptr)
		throw std::runtime_error("Missing L4D2 cvar/sound/player info interface");
	// Tickrate Enabler 1.8.0-1.9.0 registers the same command. The engine refuses a second one,
	// so keep the existing command and say where it comes from.
	if (cvar->FindCommandBase(RELIABLE_SOUND_COMMAND) != nullptr)
	{
		std::fprintf(stdout, "[L4FixesVSP] %s is already registered by another plugin (e.g. Tickrate Enabler"
			" 1.9.0, -noreliablesound disables it there); keeping that one.\n", RELIABLE_SOUND_COMMAND);
		return;
	}
	g_engine = engine;
	g_engineSound = engineSound;
	g_playerInfoManager = playerInfoManager;
	g_globals = globals;
	g_pCVar = cvar;
	ConVar_Register(0);
	m_commandsRegistered = true;
	std::fprintf(stdout, "[L4FixesVSP] %s registered.\n", RELIABLE_SOUND_COMMAND);
}

void L4FixesVSP::UnregisterCommands() noexcept
{
	if (m_commandsRegistered)
		ConVar_Unregister();
	m_commandsRegistered = false;
	g_engine = nullptr;
	g_engineSound = nullptr;
	g_playerInfoManager = nullptr;
	g_globals = nullptr;
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
		RegisterCommands(engineFactory, serverFactory, engine);
		m_loaded = true;
		std::fprintf(stdout, "[L4FixesVSP] Version %s loaded; all %zu fixes are present.\n", VERSION,
			FixDefinitions().size());
		return true;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "[L4FixesVSP] Load failed: %s\n", error.what());
		UnregisterCommands();
		m_patches.Clear();
		return false;
	}
}

void L4FixesVSP::Unload()
{
	UnregisterCommands();
	m_patches.Clear();
	m_loaded = false;
}

const char* L4FixesVSP::GetPluginDescription()
{
	static const std::string description = std::string("L4FixesVSP ") + VERSION + " - native L4D2 engine/game fixes";
	return description.c_str();
}
}

// Avoid the SDK's interface registration helpers. The engine discovers this one C export
// and negotiates the SDK's exact callback interface.
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
