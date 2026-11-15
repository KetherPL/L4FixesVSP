// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands.h"
#include "reliablesound.h"

#include "eiface.h"
#include "edict.h"
#include "engine/IEngineSound.h"
#include "game/server/iplayerinfo.h"
#include "icvar.h"
#include "tier1/convar.h"

#include <cstdio>
#include <stdexcept>

namespace l4fixes
{
namespace
{
// Interfaces used by te_reliable_sound; set while the command is registered.
IVEngineServer* g_engine = nullptr;
IEngineSound* g_engineSound = nullptr;
IPlayerInfoManager* g_playerInfoManager = nullptr;
CGlobalVars* g_globals = nullptr;

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

void KetherCommands::Register(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory, IVEngineServer* engine)
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

void KetherCommands::Unregister() noexcept
{
	if (m_commandsRegistered)
		ConVar_Unregister();
	m_commandsRegistered = false;
	g_engine = nullptr;
	g_engineSound = nullptr;
	g_playerInfoManager = nullptr;
	g_globals = nullptr;
}

}
