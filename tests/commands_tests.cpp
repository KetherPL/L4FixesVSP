// SPDX-License-Identifier: GPL-3.0-or-later
#include "kether/commands.h"
#include "kether/reliablesound.h"
#include "kether/steamreauth.h"

#include "eiface.h"
#include "edict.h"
#include "engine/IEngineSound.h"
#include "game/server/iplayerinfo.h"
#include "icvar.h"
#include "tier1/convar.h"

#include <cassert>
#include <cstring>
#include <dlfcn.h>

#ifdef NDEBUG
#error Regression tests require assertions.
#endif

using namespace l4fixes;

namespace
{
class FakePlayerInfoManager final : public IPlayerInfoManager
{
public:
	IPlayerInfo* GetPlayerInfo(edict_t*) override { return nullptr; }
	CGlobalVars* GetGlobalVars() override { return &m_globals; }

private:
	CGlobalVars m_globals{ false };
};

ICvar* g_cvar = nullptr;
FakePlayerInfoManager g_playerInfoManager;
// Only its address is used: no command runs in this test.
char g_engineSound = 0;

void* EngineFactory(const char* name, int*)
{
	if (std::strcmp(name, CVAR_INTERFACE_VERSION) == 0)
		return g_cvar;
	if (std::strcmp(name, IENGINESOUND_SERVER_INTERFACE_VERSION) == 0)
		return &g_engineSound;
	return nullptr;
}

void* ServerFactory(const char* name, int*)
{
	return std::strcmp(name, INTERFACEVERSION_PLAYERINFOMANAGER) == 0 ? &g_playerInfoManager : nullptr;
}

void ForeignCommand(const CCommand&) {}
}

// Runs against the cvar system of the SDK's libvstdlib_srv.so, the one srcds uses.
void TestCommands()
{
	void* vstdlib = dlopen("libvstdlib_srv.so", RTLD_NOW);
	assert(vstdlib != nullptr);
	auto* factory = reinterpret_cast<CreateInterfaceFn>(dlsym(vstdlib, "CreateInterface"));
	assert(factory != nullptr);
	g_cvar = static_cast<ICvar*>(factory(CVAR_INTERFACE_VERSION, nullptr));
	assert(g_cvar != nullptr);

	// Another plugin (Tickrate Enabler 1.9.0) owns te_reliable_sound before L4FixesVSP loads.
	// FCVAR_UNREGISTERED keeps it out of this image's own command list.
	ConCommand foreign(RELIABLE_SOUND_COMMAND, ForeignCommand, "", FCVAR_UNREGISTERED);
	g_cvar->RegisterConCommand(&foreign);
	assert(g_cvar->FindCommandBase(RELIABLE_SOUND_COMMAND) == &foreign);
	assert(g_cvar->FindCommandBase(STEAM_REAUTH_COMMAND) == nullptr);

	KetherCommands commands;
	commands.Register(EngineFactory, ServerFactory, nullptr);
	// The other plugin keeps its command; the re-authentication commands do not depend on it.
	assert(g_cvar->FindCommandBase(RELIABLE_SOUND_COMMAND) == &foreign);
	ConCommandBase* reauth = g_cvar->FindCommandBase(STEAM_REAUTH_COMMAND);
	ConCommandBase* status = g_cvar->FindCommandBase(STEAM_REAUTH_STATUS_COMMAND);
	assert(reauth != nullptr && reauth->IsCommand() && reauth->IsRegistered());
	assert(status != nullptr && status->IsCommand() && status->IsRegistered());
	assert(reauth != status);

	commands.Unregister();
	assert(g_cvar->FindCommandBase(STEAM_REAUTH_COMMAND) == nullptr);
	assert(g_cvar->FindCommandBase(STEAM_REAUTH_STATUS_COMMAND) == nullptr);
	// The stand-in was created by this image, so the engine may have dropped it with the others.
	g_cvar->UnregisterConCommand(&foreign);
	assert(g_cvar->FindCommandBase(RELIABLE_SOUND_COMMAND) == nullptr);
	dlclose(vstdlib);
}
