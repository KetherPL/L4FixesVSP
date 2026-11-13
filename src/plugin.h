// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "patch.h"
#include "eiface.h"

namespace l4fixes
{
inline constexpr char VERSION[] = "1.0.0";

class L4FixesVSP final : public IServerPluginCallbacks
{
public:
	bool Load(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory) override;
	void Unload() override;
	const char* GetPluginDescription() override;

	// These are permanent instruction patches and one server command, with no per-frame or
	// per-player work. Pause leaves the fixes and the command active; Unload restores patches
	// owned by this VSP and unregisters the command.
	void Pause() override {}
	void UnPause() override {}
	void LevelInit(const char*) override {}
	void ServerActivate(edict_t*, int, int) override {}
	void GameFrame(bool) override {}
	void LevelShutdown() override {}
	void ClientActive(edict_t*) override {}
	void ClientDisconnect(edict_t*) override {}
	void ClientPutInServer(edict_t*, const char*) override {}
	void SetCommandClient(int) override {}
	void ClientSettingsChanged(edict_t*) override {}
	PLUGIN_RESULT ClientConnect(bool*, edict_t*, const char*, const char*, char*, int) override { return PLUGIN_CONTINUE; }
	PLUGIN_RESULT ClientCommand(edict_t*, const CCommand&) override { return PLUGIN_CONTINUE; }
	PLUGIN_RESULT NetworkIDValidated(const char*, const char*) override { return PLUGIN_CONTINUE; }
	void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t*, EQueryCvarValueStatus, const char*, const char*) override {}

private:
	void RegisterCommands(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory, IVEngineServer* engine);
	void UnregisterCommands() noexcept;

	PatchManager m_patches;
	bool m_commandsRegistered = false;
	bool m_loaded = false;
};
}
