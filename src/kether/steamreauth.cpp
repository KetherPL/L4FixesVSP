// SPDX-License-Identifier: GPL-3.0-or-later
#include "steamreauth.h"

#include "eiface.h"
#include "edict.h"
#include "game/server/iplayerinfo.h"
#include "tier0/dbg.h"
#include "tier0/platform.h"
#include "tier1/convar.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace l4fixes
{
namespace
{
constexpr std::string_view NOTIFY_CLIENT_CONNECT = "_ZN13CSteam3Server19NotifyClientConnectEP11CBaseClientjR8netadr_sPKvj";
constexpr std::string_view NOTIFY_CLIENT_DISCONNECT = "_ZN13CSteam3Server22NotifyClientDisconnectEP11CBaseClient";
constexpr std::string_view STEAM3_SERVER = "_Z12Steam3Serverv";

// CSteam3Server::NotifyClientConnect (esi = this) hands the connecting client's ticket to Steam:
//   +A7  8B 4E 04   mov ecx, [esi+4]     m_pSteamGameServer
//   ...                                  [esp] = interface, ticket, size, Steam ID (two dwords)
//   +DA  8B 45 8C   mov eax, [ebp-74h]   the interface's vtable
//   +DD  FF 50 68   call [eax+68h]       ISteamGameServer::BeginAuthSession (slot 26)
//   +E0  83 F8 05   cmp eax, 5           the result switch follows
// The vtable load and the indirect call become a direct call to BeginAuthSessionHook, which takes
// the same stack arguments, makes the virtual call itself and returns its result in eax.
constexpr std::size_t GAME_SERVER_FIELD_OFFSET = 0xA7;
constexpr std::array<Byte, 3> GAME_SERVER_FIELD_CODE = { 0x8B, 0x4E, 0x04 };
constexpr std::size_t GAME_SERVER_FIELD = 4;
constexpr std::size_t CALL_SITE_OFFSET = 0xDA;
constexpr std::array<Byte, 9> CALL_SITE_CODE = { 0x8B, 0x45, 0x8C, 0xFF, 0x50, 0x68, 0x83, 0xF8, 0x05 };
constexpr std::size_t BEGIN_AUTH_SESSION_SLOT = 0x68 / sizeof(void*);
// CSteam3Server::NotifyClientDisconnect:
//   +7D  FF 56 6C   call [esi+6Ch]       ISteamGameServer::EndAuthSession (slot 27)
constexpr std::size_t END_AUTH_SESSION_OFFSET = 0x7D;
constexpr std::array<Byte, 3> END_AUTH_SESSION_CODE = { 0xFF, 0x56, 0x6C };
constexpr std::size_t END_AUTH_SESSION_SLOT = 0x6C / sizeof(void*);

constexpr double SCAN_INTERVAL = 5.0;

// i386 passes 'this' and a by-value CSteamID (two dwords) on the stack like ordinary arguments.
using BeginAuthSessionFn = int (*)(void*, const void*, int, std::uint32_t, std::uint32_t);
using EndAuthSessionFn = void (*)(void*, std::uint32_t, std::uint32_t);
using Steam3ServerFn = void* (*)();

AuthRetryTracker g_tracker;
IVEngineServer* g_engine = nullptr;
CGlobalVars* g_globals = nullptr;
// The interface the engine last began an auth session on, and the engine's CSteam3Server accessor.
void* g_gameServer = nullptr;
Steam3ServerFn g_steam3Server = nullptr;
double g_nextScan = 0.0;

template <typename Function>
Function VirtualFunction(void* object, std::size_t slot)
{
	void** table;
	std::memcpy(&table, object, sizeof(table));
	return reinterpret_cast<Function>(table[slot]);
}

std::uint64_t SteamId(std::uint32_t low, std::uint32_t high)
{
	return static_cast<std::uint64_t>(high) << 32 | low;
}

// Called by the engine in place of ISteamGameServer::BeginAuthSession, with its arguments.
int BeginAuthSessionHook(void* gameServer, const void* ticket, int size, std::uint32_t idLow, std::uint32_t idHigh) noexcept
{
	const int result = VirtualFunction<BeginAuthSessionFn>(gameServer, BEGIN_AUTH_SESSION_SLOT)(
		gameServer, ticket, size, idLow, idHigh);
	// Any other result makes the engine reject the connection.
	if (result != AUTH_BEGIN_OK || ticket == nullptr || size <= 0)
		return result;
	try
	{
		g_gameServer = gameServer;
		g_tracker.RecordTicket(SteamId(idLow, idHigh),
			std::span(static_cast<const Byte*>(ticket), static_cast<std::size_t>(size)), Plat_FloatTime());
	}
	catch (...)
	{
		// Out of memory: the client connects as usual and is simply never retried.
	}
	return result;
}

// The engine's current ISteamGameServer, or null if it is not the one the tickets were given to
// (Steam was shut down or restarted since, which also ended every auth session).
void* LiveGameServer()
{
	if (g_steam3Server == nullptr || g_gameServer == nullptr)
		return nullptr;
	const auto* steam3Server = static_cast<const Byte*>(g_steam3Server());
	void* live;
	std::memcpy(&live, steam3Server + GAME_SERVER_FIELD, sizeof(live));
	return live == g_gameServer ? live : nullptr;
}

void ExpectCode(const FunctionSymbol& function, std::size_t offset, std::span<const Byte> expected, const char* what)
{
	if (offset > function.size || expected.size() > function.size - offset
		|| ReadMemory(function.address + offset, expected.size()) != std::vector<Byte>(expected.begin(), expected.end()))
		throw std::runtime_error(std::string("Unexpected engine code for Steam re-authentication: ") + what);
}

// Connected humans: bots, SourceTV and free slots have no net channel.
std::vector<AuthClient> ConnectedClients()
{
	std::vector<AuthClient> clients;
	if (g_engine == nullptr || g_globals == nullptr || g_globals->pEdicts == nullptr)
		return clients;
	for (int index = 1; index <= g_globals->maxClients; ++index)
	{
		if (g_engine->GetPlayerNetInfo(index) == nullptr)
			continue;
		edict_t* edict = &g_globals->pEdicts[index];
		// The engine returns the ID the client's ticket claimed, validated or not. CSteamID is
		// its 64-bit value.
		const CSteamID* steamId = g_engine->GetClientSteamID(edict);
		if (steamId == nullptr)
			continue;
		std::uint64_t value;
		std::memcpy(&value, steamId, sizeof(value));
		clients.push_back({ index, value, g_engine->IsClientFullyAuthenticated(edict) });
	}
	return clients;
}

const char* ResultName(int result)
{
	switch (result)
	{
	case AUTH_BEGIN_OK: return "accepted the ticket, Steam's answer follows";
	case 1: return "rejected the ticket (invalid)";
	case AUTH_BEGIN_DUPLICATE: return "reported a duplicate request";
	case 3: return "rejected the ticket (invalid version)";
	case 4: return "rejected the ticket (wrong game)";
	case 5: return "rejected the ticket (expired)";
	case AUTH_BEGIN_UNAVAILABLE: return "was skipped, the engine's Steam interface changed";
	default: return "returned an unknown result";
	}
}

// To the console and, like the engine's own STEAMAUTH lines, to the server log.
__attribute__((format(printf, 1, 2))) void Log(const char* format, ...)
{
	char text[512];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	Msg("[L4FixesVSP] %s\n", text);
	if (g_engine != nullptr)
	{
		char line[560];
		std::snprintf(line, sizeof(line), "[L4FixesVSP] %s\n", text);
		g_engine->LogPrint(line);
	}
}

void Report(const AuthRetryEvent& event)
{
	const auto steamId = static_cast<unsigned long long>(event.steamId);
	if (event.kind == AuthRetryEvent::Kind::Validated)
	{
		Log("Steam re-auth: client %d (%llu) was validated by Steam %.1f s after attempt %d.",
			event.client, steamId, event.answeredAfter, event.attempts);
		return;
	}
	char next[64] = "no automatic attempt follows";
	if (event.retryIn > 0.0)
		std::snprintf(next, sizeof(next), "next attempt in %.0f s", event.retryIn);
	Log("Steam re-auth: client %d (%llu) attempt %d: BeginAuthSession %s (%d); %s.",
		event.client, steamId, event.attempts, ResultName(event.result), event.result, next);
}

// What Steam said about a ticket's last attempt, as far as a plugin can tell: the engine reports
// a positive answer to plugins and prints a negative one to the server console only.
std::string AnswerText(const AuthTicket& ticket, double now)
{
	if (ticket.attempts == 0)
		return {};
	char text[128];
	if (ticket.answeredAttempt == ticket.attempts)
		std::snprintf(text, sizeof(text), "  attempt %d: validated by Steam after %.1f s",
			ticket.attempts, ticket.answerDelay);
	else
		std::snprintf(text, sizeof(text), "  attempt %d: no positive answer from Steam in %.0f s",
			ticket.attempts, std::max(0.0, now - ticket.lastAttempt));
	return text;
}

// l4fixes_steam_reauth_status
void StatusCommand(const CCommand&)
{
	if (g_engine == nullptr)
	{
		Warning("[L4FixesVSP] %s: the plugin is not loaded.\n", STEAM_REAUTH_STATUS_COMMAND);
		return;
	}
	try
	{
		const double now = Plat_FloatTime();
		const auto clients = ConnectedClients();
		Msg("[L4FixesVSP] Steam re-authentication: %zu client(s), %zu ticket(s) stored.\n",
			clients.size(), g_tracker.Tickets().size());
		for (const auto& client : clients)
		{
			const AuthTicket* ticket = g_tracker.Find(client.steamId);
			const auto steamId = static_cast<unsigned long long>(client.steamId);
			const char* state = client.authenticated ? "authenticated" : "NOT authenticated";
			if (ticket == nullptr)
				Msg("  client %d  %llu  %s  no ticket (connected before the plugin loaded)\n", client.index, steamId, state);
			else if (client.authenticated || ticket->abandoned)
				Msg("  client %d  %llu  %s  ticket %zu bytes  attempts %d%s%s\n", client.index, steamId, state,
					ticket->ticket.size(), ticket->attempts, ticket->abandoned ? " (given up)" : "",
					AnswerText(*ticket, now).c_str());
			else
				Msg("  client %d  %llu  %s  ticket %zu bytes  attempts %d  next in %.0f s%s\n", client.index, steamId, state,
					ticket->ticket.size(), ticket->attempts, std::max(0.0, ticket->nextAttempt - now),
					AnswerText(*ticket, now).c_str());
		}
	}
	catch (const std::exception& error)
	{
		Warning("[L4FixesVSP] %s: %s\n", STEAM_REAUTH_STATUS_COMMAND, error.what());
	}
}

// l4fixes_steam_reauth [client]
void ReauthCommand(const CCommand& args)
{
	if (g_engine == nullptr)
	{
		Warning("[L4FixesVSP] %s: the plugin is not loaded.\n", STEAM_REAUTH_COMMAND);
		return;
	}
	try
	{
		char* end = nullptr;
		const long wanted = args.ArgC() > 1 ? std::strtol(args.Arg(1), &end, 10) : 0;
		if (args.ArgC() > 2 || (args.ArgC() > 1 && (end == args.Arg(1) || *end != '\0' || wanted < 1)))
		{
			Warning("[L4FixesVSP] Usage: %s [client index]\n", STEAM_REAUTH_COMMAND);
			return;
		}
		const double now = Plat_FloatTime();
		int attempts = 0;
		for (const auto& client : ConnectedClients())
		{
			// Without an index only the clients Steam has not validated are retried.
			if (wanted != 0 ? client.index != wanted : client.authenticated)
				continue;
			if (const auto event = g_tracker.Force(client, now, SteamReauthenticate))
			{
				Report(*event);
				++attempts;
			}
			else
			{
				Log("Steam re-auth: client %d (%llu) has no stored ticket.", client.index,
					static_cast<unsigned long long>(client.steamId));
			}
		}
		if (attempts == 0)
			Msg("[L4FixesVSP] %s: nothing to retry.\n", STEAM_REAUTH_COMMAND);
	}
	catch (const std::exception& error)
	{
		Warning("[L4FixesVSP] %s: %s\n", STEAM_REAUTH_COMMAND, error.what());
	}
}

// Registered and unregistered with the plugin's other commands (KetherCommands).
ConCommand g_statusCommand(STEAM_REAUTH_STATUS_COMMAND, StatusCommand,
	"List the connected clients, whether Steam validated them and their stored auth tickets");
ConCommand g_reauthCommand(STEAM_REAUTH_COMMAND, ReauthCommand,
	"Submit stored Steam auth tickets again now: without an argument for every client Steam has not"
	" validated, with a client index for that client even if it is validated");
}

void InstallSteamReauth(PatchManager& patches, const LoadedModule& engine)
{
	const auto connect = engine.ResolveFunction(NOTIFY_CLIENT_CONNECT);
	const auto disconnect = engine.ResolveFunction(NOTIFY_CLIENT_DISCONNECT);
	const auto steam3Server = engine.ResolveFunction(STEAM3_SERVER);
	ExpectCode(connect, GAME_SERVER_FIELD_OFFSET, GAME_SERVER_FIELD_CODE, "CSteam3Server's Steam interface");
	ExpectCode(disconnect, END_AUTH_SESSION_OFFSET, END_AUTH_SESSION_CODE, "EndAuthSession call");
	if (CALL_SITE_OFFSET > connect.size || CALL_SITE_CODE.size() > connect.size - CALL_SITE_OFFSET)
		throw std::runtime_error("Unexpected engine code for Steam re-authentication: BeginAuthSession call");
	const std::uintptr_t site = connect.address + CALL_SITE_OFFSET;
	// call rel32; nop; the original cmp stays.
	std::array<Byte, 9> replacement = { 0xE8, 0x00, 0x00, 0x00, 0x00, 0x90, 0x83, 0xF8, 0x05 };
	const auto displacement = static_cast<std::uint32_t>(SteamReauthHookAddress() - (site + 5));
	std::memcpy(replacement.data() + 1, &displacement, sizeof(displacement));
	patches.Register(std::make_unique<CodePatch>("Steam auth ticket capture", site, CALL_SITE_CODE, replacement));
	g_steam3Server = reinterpret_cast<Steam3ServerFn>(steam3Server.address);
}

void StartSteamReauth(CreateInterfaceFn serverFactory, IVEngineServer* engine)
{
	auto* playerInfoManager = serverFactory != nullptr
		? static_cast<IPlayerInfoManager*>(serverFactory(INTERFACEVERSION_PLAYERINFOMANAGER, nullptr)) : nullptr;
	CGlobalVars* globals = playerInfoManager != nullptr ? playerInfoManager->GetGlobalVars() : nullptr;
	if (engine == nullptr || globals == nullptr)
		throw std::runtime_error("Missing L4D2 player info interface");
	g_engine = engine;
	g_globals = globals;
	g_nextScan = 0.0;
}

void SteamReauthFrame()
{
	if (g_engine == nullptr || g_tracker.Empty())
		return;
	const double now = Plat_FloatTime();
	if (now < g_nextScan)
		return;
	g_nextScan = now + SCAN_INTERVAL;
	try
	{
		for (const auto& event : g_tracker.Update(now, ConnectedClients(), SteamReauthenticate))
			Report(event);
	}
	catch (const std::exception& error)
	{
		Warning("[L4FixesVSP] Steam re-auth: %s\n", error.what());
	}
}

void SteamReauthValidated(const char* networkId) noexcept
{
	if (g_engine == nullptr || networkId == nullptr)
		return;
	try
	{
		const auto account = SteamAccountId(networkId);
		if (!account)
			return;
		if (const auto event = g_tracker.RecordValidation(*account, Plat_FloatTime()))
			Report(*event);
	}
	catch (...)
	{
		// Only the report is lost.
	}
}

void StopSteamReauth() noexcept
{
	g_tracker.Clear();
	g_engine = nullptr;
	g_globals = nullptr;
	g_gameServer = nullptr;
	g_steam3Server = nullptr;
	g_nextScan = 0.0;
}

std::uintptr_t SteamReauthHookAddress() noexcept
{
	return reinterpret_cast<std::uintptr_t>(&BeginAuthSessionHook);
}

std::span<const Byte> SteamReauthCallSiteBytes() noexcept
{
	return CALL_SITE_CODE;
}

std::size_t SteamReauthCallSiteOffset() noexcept
{
	return CALL_SITE_OFFSET;
}

std::string_view SteamReauthCallSiteSymbol() noexcept
{
	return NOTIFY_CLIENT_CONNECT;
}

AuthRetryTracker& SteamReauthTracker() noexcept
{
	return g_tracker;
}

int SteamReauthenticate(std::uint64_t steamId, std::span<const Byte> ticket)
{
	void* gameServer = LiveGameServer();
	if (gameServer == nullptr)
		return AUTH_BEGIN_UNAVAILABLE;
	const auto low = static_cast<std::uint32_t>(steamId);
	const auto high = static_cast<std::uint32_t>(steamId >> 32);
	// A second BeginAuthSession for a Steam ID whose session is still open is a duplicate request.
	VirtualFunction<EndAuthSessionFn>(gameServer, END_AUTH_SESSION_SLOT)(gameServer, low, high);
	return VirtualFunction<BeginAuthSessionFn>(gameServer, BEGIN_AUTH_SESSION_SLOT)(
		gameServer, ticket.data(), static_cast<int>(ticket.size()), low, high);
}
}
