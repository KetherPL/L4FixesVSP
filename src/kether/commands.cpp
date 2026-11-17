// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands.h"
#include "reliablesound.h"
#include "steamreauth.h"

#include "eiface.h"
#include "edict.h"
#include "engine/IEngineSound.h"
#include "game/server/iplayerinfo.h"
#include "icvar.h"
#include "tier1/convar.h"

#include <cstdio>
#include <stdexcept>
#include <string>

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

// tier1 links the plugin's commands into its list when the library is loaded; ConVar_Register hands
// the list to the engine through CommandAccessor.
ConCommand g_reliableSoundCommand(RELIABLE_SOUND_COMMAND, ReliableSoundCommand,
	"Emit a precached sound reliably: <entity> <channel> <soundlevel> <volume> <pitch> <flags>"
	" <x,y,z|-> \"<sample>\" [client,client,...]");

void AppendName(std::string& names, const char* name)
{
	if (!names.empty())
		names += ", ";
	names += name;
}

// Registers the plugin's commands one by one. A name the engine already has belongs to another
// plugin (Tickrate Enabler 1.8.0-1.9.0 registers te_reliable_sound); that command is kept and
// only ours with the same name is left out, so the plugin's other commands do not depend on it.
class CommandAccessor final : public IConCommandBaseAccessor
{
public:
	bool RegisterConCommandBase(ConCommandBase* command) override
	{
		if (g_pCVar->FindCommandBase(command->GetName()) != nullptr)
		{
			AppendName(m_taken, command->GetName());
			return false;
		}
		g_pCVar->RegisterConCommand(command);
		AppendName(m_registered, command->GetName());
		return true;
	}

	const std::string& Registered() const noexcept { return m_registered; }
	const std::string& Taken() const noexcept { return m_taken; }

private:
	std::string m_registered;
	std::string m_taken;
};

// tier1 keeps the pointer for commands created later, so the accessor lives as long as the plugin.
CommandAccessor g_commandAccessor;
}

void KetherCommands::Register(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory, IVEngineServer* engine)
{
	auto* cvar = static_cast<ICvar*>(engineFactory(CVAR_INTERFACE_VERSION, nullptr));
	auto* engineSound = static_cast<IEngineSound*>(engineFactory(IENGINESOUND_SERVER_INTERFACE_VERSION, nullptr));
	auto* playerInfoManager = static_cast<IPlayerInfoManager*>(serverFactory(INTERFACEVERSION_PLAYERINFOMANAGER, nullptr));
	CGlobalVars* globals = playerInfoManager != nullptr ? playerInfoManager->GetGlobalVars() : nullptr;
	if (cvar == nullptr || engineSound == nullptr || playerInfoManager == nullptr || globals == nullptr)
		throw std::runtime_error("Missing L4D2 cvar/sound/player info interface");
	g_engine = engine;
	g_engineSound = engineSound;
	g_playerInfoManager = playerInfoManager;
	g_globals = globals;
	g_pCVar = cvar;
	ConVar_Register(0, &g_commandAccessor);
	m_commandsRegistered = true;
	if (!g_commandAccessor.Registered().empty())
		std::fprintf(stdout, "[L4FixesVSP] Commands registered: %s.\n", g_commandAccessor.Registered().c_str());
	if (!g_commandAccessor.Taken().empty())
		std::fprintf(stdout, "[L4FixesVSP] Already registered by another plugin (e.g. Tickrate Enabler 1.9.0,"
			" -noreliablesound disables it there), keeping that one: %s.\n", g_commandAccessor.Taken().c_str());
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
