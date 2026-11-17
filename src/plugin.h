// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "patch.h"
#include "kether/commands.h"
#include "eiface.h"

namespace l4fixes
{
inline constexpr char VERSION[] = "1.1.0";

class L4FixesVSP final : public IServerPluginCallbacks
{
public:
	bool Load(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory) override;
	void Unload() override;
	const char* GetPluginDescription() override;

	// These are permanent instruction patches and server commands. The only per-frame work is
	// Steam re-authentication (GameFrame), which looks at the clients every few seconds. Pause
	// leaves the fixes and the commands active but stops those retries; Unload restores patches
	// owned by this VSP and unregisters the commands.
	void Pause() override {}
	void UnPause() override {}
	void LevelInit(const char*) override {}
	void ServerActivate(edict_t*, int, int) override {}
	void GameFrame(bool) override;
	void LevelShutdown() override {}
	void ClientActive(edict_t*) override {}
	void ClientDisconnect(edict_t*) override {}
	void ClientPutInServer(edict_t*, const char*) override {}
	void SetCommandClient(int) override {}
	void ClientSettingsChanged(edict_t*) override {}
	PLUGIN_RESULT ClientConnect(bool*, edict_t*, const char*, const char*, char*, int) override { return PLUGIN_CONTINUE; }
	PLUGIN_RESULT ClientCommand(edict_t*, const CCommand&) override { return PLUGIN_CONTINUE; }
	PLUGIN_RESULT NetworkIDValidated(const char* userName, const char* networkId) override;
	void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t*, EQueryCvarValueStatus, const char*, const char*) override {}

private:
	PatchManager m_patches;
	KetherCommands m_commands;
	bool m_loaded = false;
};
}
