// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "authretry.h"
#include "../patch.h"
#include "interface.h"

class IVEngineServer;

namespace l4fixes
{
inline constexpr char STEAM_REAUTH_COMMAND[] = "l4fixes_steam_reauth";
inline constexpr char STEAM_REAUTH_STATUS_COMMAND[] = "l4fixes_steam_reauth_status";

// Steam re-authentication. The "No Steam logon" fix keeps a client whose auth ticket Steam did not
// validate, but the engine asks Steam only once, at connect, so such a client stays unauthenticated
// (and unknown to SourceMod) until it reconnects. This asks again: the engine's BeginAuthSession call
// in CSteam3Server::NotifyClientConnect is routed through the plugin, which keeps each accepted
// ticket, and every few seconds a client that is still unauthenticated has its auth session ended
// and begun again with that ticket. Steam's answer goes through the engine's own callback
// (CSteam3Server::OnValidateAuthTicketResponse), which marks the client authenticated, prints
// "STEAM USERID validated" and tells the plugins; a negative answer is handled like the first one.
//
// Unlike the other fixes this one redirects an engine call into the plugin and does per-frame work.

// Resolves and verifies the engine code involved and registers the call-site patch with 'patches'.
// Throws if anything differs from the supported engine build; nothing is written before EnableAll.
void InstallSteamReauth(PatchManager& patches, const LoadedModule& engine);

// Starts the periodic retries. Throws if a game interface is missing.
void StartSteamReauth(CreateInterfaceFn serverFactory, IVEngineServer* engine);

// Called every server frame; scans the clients every few seconds.
void SteamReauthFrame();

// The engine's NetworkIDValidated plugin callback: Steam answered a client's auth request
// positively. Records and reports it if it answers an attempt made here.
void SteamReauthValidated(const char* networkId) noexcept;

// Stops the retries and forgets every ticket. The call-site patch belongs to the PatchManager.
void StopSteamReauth() noexcept;

// Exposed for the regression tests.
std::uintptr_t SteamReauthHookAddress() noexcept;
std::span<const Byte> SteamReauthCallSiteBytes() noexcept;
std::size_t SteamReauthCallSiteOffset() noexcept;
std::string_view SteamReauthCallSiteSymbol() noexcept;
AuthRetryTracker& SteamReauthTracker() noexcept;
int SteamReauthenticate(std::uint64_t steamId, std::span<const Byte> ticket);
}
