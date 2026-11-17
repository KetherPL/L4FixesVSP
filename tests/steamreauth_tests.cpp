// SPDX-License-Identifier: GPL-3.0-or-later
#include "kether/steamreauth.h"

#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>

#ifdef NDEBUG
#error Regression tests require assertions.
#endif

using namespace l4fixes;

namespace
{
constexpr std::uint64_t ID_A = 0x0110000100000001ULL;
constexpr std::uint64_t ID_B = 0x0110000100000002ULL;
constexpr std::uint32_t ACCOUNT_A = 1;
constexpr std::uint32_t ACCOUNT_B = 2;
constexpr std::array<Byte, 3> TICKET_A = { 0x01, 0x02, 0x03 };
constexpr std::array<Byte, 2> TICKET_B = { 0x04, 0x05 };

template <typename Operation>
void ExpectFailure(Operation operation)
{
	bool failed = false;
	try { operation(); }
	catch (const std::exception&) { failed = true; }
	assert(failed);
}

template <typename Bytes>
std::vector<Byte> Copy(const Bytes& bytes)
{
	return std::vector<Byte>(bytes.begin(), bytes.end());
}

struct RetryCall
{
	std::uint64_t steamId;
	std::vector<Byte> ticket;
};

void TestSchedule()
{
	AuthRetryTracker tracker;
	std::vector<RetryCall> calls;
	const AuthRetryFn retry = [&](std::uint64_t steamId, std::span<const Byte> ticket)
	{
		calls.push_back({ steamId, Copy(ticket) });
		return AUTH_BEGIN_OK;
	};

	// Tickets that cannot be kept.
	assert(!tracker.RecordTicket(0, TICKET_A, 0.0));
	assert(!tracker.RecordTicket(ID_A, {}, 0.0));
	assert(!tracker.RecordTicket(ID_A, std::vector<Byte>(AUTH_MAX_TICKET_SIZE + 1), 0.0));
	assert(tracker.Empty());

	assert(tracker.RecordTicket(ID_A, TICKET_A, 100.0));
	assert(tracker.RecordTicket(ID_B, TICKET_B, 100.0));
	const std::array<AuthClient, 2> clients = { AuthClient{ 1, ID_A, true }, AuthClient{ 2, ID_B, false } };

	// Steam gets time to answer the engine's own request first.
	assert(tracker.Update(100.0 + AUTH_FIRST_DELAY - 1.0, clients, retry).empty() && calls.empty());

	// Only the client Steam has not validated is retried, with its own ticket.
	auto events = tracker.Update(100.0 + AUTH_FIRST_DELAY, clients, retry);
	assert(events.size() == 1 && calls.size() == 1);
	assert(calls[0].steamId == ID_B && calls[0].ticket == Copy(TICKET_B));
	assert(events[0].kind == AuthRetryEvent::Kind::Attempt && events[0].client == 2 && events[0].steamId == ID_B);
	assert(events[0].attempts == 1 && events[0].result == AUTH_BEGIN_OK && events[0].retryIn == 2 * AUTH_FIRST_DELAY);

	// The delay doubles up to the maximum, and the attempts end.
	double now = 100.0 + AUTH_FIRST_DELAY;
	for (int attempt = 2; attempt <= AUTH_MAX_ATTEMPTS; ++attempt)
	{
		const double delay = events[0].retryIn;
		assert(delay > 0.0 && delay <= AUTH_MAX_DELAY);
		assert(tracker.Update(now + delay - 1.0, clients, retry).empty());
		now += delay;
		events = tracker.Update(now, clients, retry);
		assert(events.size() == 1 && events[0].attempts == attempt);
		if (attempt == 2)
			assert(events[0].retryIn == 4 * AUTH_FIRST_DELAY);
	}
	assert(events[0].retryIn == 0.0 && tracker.Find(ID_B)->abandoned);
	assert(calls.size() == static_cast<std::size_t>(AUTH_MAX_ATTEMPTS));
	assert(tracker.Update(now + 10 * AUTH_MAX_DELAY, clients, retry).empty());
	assert(calls.size() == static_cast<std::size_t>(AUTH_MAX_ATTEMPTS));
	assert(tracker.Find(ID_A)->attempts == 0);
}

void TestNetworkIds()
{
	assert(SteamAccountId("STEAM_1:0:0") == std::uint32_t{ 0 });
	assert(SteamAccountId("STEAM_1:1:5") == std::uint32_t{ 11 });
	assert(SteamAccountId("STEAM_0:0:2147483647") == std::uint32_t{ 0xFFFFFFFEu });
	for (const char* other : { "", "BOT", "STEAM_ID_LAN", "STEAM_ID_PENDING", "STEAM_1:0", "STEAM_1:0:", "STEAM_1:2:5",
		"STEAM_1:0:5x", "STEAM_:0:5", "STEAM_1:0:2147483648", "STEAM_1:0:-5", "[U:1:11]" })
		assert(!SteamAccountId(other));
}

void TestOutcomes()
{
	AuthRetryTracker tracker;
	int result = AUTH_BEGIN_OK;
	int calls = 0;
	const AuthRetryFn retry = [&](std::uint64_t, std::span<const Byte>) { ++calls; return result; };
	std::array<AuthClient, 1> client = { AuthClient{ 3, ID_A, false } };

	// Steam's answer to the engine's own request at connect is not an answer to an attempt.
	assert(tracker.RecordTicket(ID_A, TICKET_A, 0.0));
	assert(!tracker.RecordValidation(ACCOUNT_A, 1.0));

	// Steam validates the client after an attempt: reported once, when the engine says so.
	assert(tracker.Update(AUTH_FIRST_DELAY, client, retry).size() == 1);
	assert(!tracker.RecordValidation(ACCOUNT_B, AUTH_FIRST_DELAY + 1.0));
	auto answer = tracker.RecordValidation(ACCOUNT_A, AUTH_FIRST_DELAY + 2.0);
	assert(answer && answer->kind == AuthRetryEvent::Kind::Validated && answer->client == 3);
	assert(answer->steamId == ID_A && answer->attempts == 1 && answer->answeredAfter == 2.0);
	assert(!tracker.RecordValidation(ACCOUNT_A, AUTH_FIRST_DELAY + 3.0));
	client[0].authenticated = true;
	assert(tracker.Update(AUTH_FIRST_DELAY + 10.0, client, retry).empty() && calls == 1);

	// A forced attempt works on a validated client too. Nothing follows it automatically, and the
	// client being authenticated already does not count as Steam's answer to it.
	const auto forced = tracker.Force(client[0], 50.0, retry);
	assert(forced && forced->kind == AuthRetryEvent::Kind::Attempt && forced->attempts == 2 && calls == 2);
	assert(forced->retryIn == 0.0);
	assert(tracker.Update(55.0, client, retry).empty() && tracker.Find(ID_A)->answeredAttempt == 1);
	answer = tracker.RecordValidation(ACCOUNT_A, 50.5);
	assert(answer && answer->attempts == 2 && answer->answeredAfter == 0.5);
	assert(tracker.Find(ID_A)->answeredAttempt == 2 && tracker.Find(ID_A)->answerDelay == 0.5);
	assert(!tracker.Force(AuthClient{ 4, ID_B, false }, 50.0, retry) && calls == 2);

	// A new ticket for the same Steam ID (a reconnect) starts over.
	client[0].authenticated = false;
	assert(tracker.RecordTicket(ID_A, TICKET_B, 1000.0));
	assert(tracker.Tickets().size() == 1 && tracker.Find(ID_A)->attempts == 0);
	assert(tracker.Find(ID_A)->ticket == Copy(TICKET_B) && tracker.Find(ID_A)->answeredAttempt == 0);

	// A duplicate request or an unusable interface is retried later; a rejected ticket is not.
	result = AUTH_BEGIN_DUPLICATE;
	auto events = tracker.Update(1000.0 + AUTH_FIRST_DELAY, client, retry);
	assert(events.size() == 1 && events[0].result == AUTH_BEGIN_DUPLICATE && events[0].retryIn > 0.0);
	result = AUTH_BEGIN_UNAVAILABLE;
	events = tracker.Update(2000.0, client, retry);
	assert(events.size() == 1 && events[0].result == AUTH_BEGIN_UNAVAILABLE && events[0].retryIn > 0.0);
	result = 5;
	events = tracker.Update(3000.0, client, retry);
	assert(events.size() == 1 && events[0].result == 5 && events[0].retryIn == 0.0);
	const int before = calls;
	assert(tracker.Update(9000.0, client, retry).empty() && calls == before);

	// A client that was validated without any attempt is not reported.
	AuthRetryTracker quiet;
	assert(quiet.RecordTicket(ID_B, TICKET_B, 0.0));
	const std::array<AuthClient, 1> validated = { AuthClient{ 1, ID_B, true } };
	assert(quiet.Update(1.0, validated, retry).empty() && !quiet.RecordValidation(ACCOUNT_B, 2.0));
}

void TestForgetting()
{
	AuthRetryTracker tracker;
	const AuthRetryFn retry = [](std::uint64_t, std::span<const Byte>) { return AUTH_BEGIN_OK; };
	assert(tracker.RecordTicket(ID_A, TICKET_A, 0.0));
	assert(tracker.RecordTicket(ID_B, TICKET_B, 0.0));
	const std::array<AuthClient, 1> client = { AuthClient{ 1, ID_A, true } };

	// A ticket whose client never showed up (rejected connection) or left is dropped; a connected
	// client's ticket is kept for as long as it stays.
	tracker.Update(AUTH_FORGET_AFTER, client, retry);
	assert(tracker.Tickets().size() == 2);
	tracker.Update(AUTH_FORGET_AFTER + 1.0, client, retry);
	assert(tracker.Tickets().size() == 1 && tracker.Find(ID_A) != nullptr && tracker.Find(ID_B) == nullptr);
	tracker.Update(10 * AUTH_FORGET_AFTER, client, retry);
	assert(tracker.Find(ID_A) != nullptr);
	tracker.Update(11 * AUTH_FORGET_AFTER + 1.0, {}, retry);
	assert(tracker.Empty());

	// A full store replaces the ticket unseen for the longest.
	for (std::size_t index = 0; index < AUTH_MAX_TICKETS; ++index)
		assert(tracker.RecordTicket(ID_A + index, TICKET_A, static_cast<double>(index)));
	assert(tracker.RecordTicket(ID_A + AUTH_MAX_TICKETS, TICKET_B, 500.0));
	assert(tracker.Tickets().size() == AUTH_MAX_TICKETS);
	assert(tracker.Find(ID_A) == nullptr && tracker.Find(ID_A + 1) != nullptr);
	assert(tracker.Find(ID_A + AUTH_MAX_TICKETS)->ticket == Copy(TICKET_B));
	tracker.Clear();
	assert(tracker.Empty());
}

// A stand-in for the engine's ISteamGameServer: only the two auth session slots are used.
struct FakeCall
{
	bool begin;
	void* self;
	std::uint64_t steamId;
	std::vector<Byte> ticket;
};
std::vector<FakeCall> g_fakeCalls;
int g_beginResult = AUTH_BEGIN_OK;

int FakeBeginAuthSession(void* self, const void* ticket, int size, std::uint32_t low, std::uint32_t high)
{
	const auto* bytes = static_cast<const Byte*>(ticket);
	g_fakeCalls.push_back({ true, self, static_cast<std::uint64_t>(high) << 32 | low,
		std::vector<Byte>(bytes, bytes + size) });
	return g_beginResult;
}

void FakeEndAuthSession(void* self, std::uint32_t low, std::uint32_t high)
{
	g_fakeCalls.push_back({ false, self, static_cast<std::uint64_t>(high) << 32 | low, {} });
}

struct FakeGameServer
{
	void** table;
};

void TestHook(const LoadedModule& module, void* steam3Server)
{
	std::array<void*, 28> table = {};
	table[26] = reinterpret_cast<void*>(&FakeBeginAuthSession);
	table[27] = reinterpret_cast<void*>(&FakeEndAuthSession);
	FakeGameServer gameServer = { table.data() };
	FakeGameServer otherGameServer = { table.data() };
	auto* gameServerField = static_cast<Byte*>(steam3Server) + sizeof(void*);
	const auto setEngineInterface = [&](void* interface) { std::memcpy(gameServerField, &interface, sizeof(interface)); };

	const auto function = module.ResolveFunction(SteamReauthCallSiteSymbol());
	const std::uintptr_t site = function.address + SteamReauthCallSiteOffset();
	const auto original = ReadMemory(site, SteamReauthCallSiteBytes().size());
	assert(original == Copy(SteamReauthCallSiteBytes()));

	// The call site calls the plugin's hook while enabled and is restored afterwards.
	{
		PatchManager manager;
		InstallSteamReauth(manager, module);
		assert(ReadMemory(site, original.size()) == original);
		manager.EnableAll();
		const auto patched = ReadMemory(site, original.size());
		assert(patched[0] == 0xE8 && patched[5] == 0x90);
		assert(std::equal(patched.begin() + 6, patched.end(), original.begin() + 6));
		std::uint32_t displacement;
		std::memcpy(&displacement, patched.data() + 1, sizeof(displacement));
		assert(static_cast<std::uint32_t>(site + 5) + displacement == static_cast<std::uint32_t>(SteamReauthHookAddress()));
	}
	assert(ReadMemory(site, original.size()) == original);

	// The hook forwards the engine's arguments and result, and keeps accepted tickets only.
	using Hook = int (*)(void*, const void*, int, std::uint32_t, std::uint32_t);
	const auto hook = reinterpret_cast<Hook>(SteamReauthHookAddress());
	const auto low = static_cast<std::uint32_t>(ID_A);
	const auto high = static_cast<std::uint32_t>(ID_A >> 32);
	auto& tracker = SteamReauthTracker();
	g_beginResult = 1;
	assert(hook(&gameServer, TICKET_A.data(), static_cast<int>(TICKET_A.size()), low, high) == 1);
	assert(tracker.Empty() && g_fakeCalls.size() == 1);
	assert(g_fakeCalls[0].begin && g_fakeCalls[0].self == &gameServer && g_fakeCalls[0].steamId == ID_A);
	assert(g_fakeCalls[0].ticket == Copy(TICKET_A));
	g_beginResult = AUTH_BEGIN_OK;
	assert(hook(&gameServer, TICKET_A.data(), static_cast<int>(TICKET_A.size()), low, high) == AUTH_BEGIN_OK);
	assert(tracker.Find(ID_A) != nullptr && tracker.Find(ID_A)->ticket == Copy(TICKET_A));
	g_fakeCalls.clear();

	// A retry ends the session and begins it again, on the engine's current interface only.
	assert(SteamReauthenticate(ID_A, TICKET_A) == AUTH_BEGIN_UNAVAILABLE && g_fakeCalls.empty());
	setEngineInterface(&otherGameServer);
	assert(SteamReauthenticate(ID_A, TICKET_A) == AUTH_BEGIN_UNAVAILABLE && g_fakeCalls.empty());
	setEngineInterface(&gameServer);
	g_beginResult = AUTH_BEGIN_DUPLICATE;
	assert(SteamReauthenticate(ID_A, TICKET_A) == AUTH_BEGIN_DUPLICATE);
	assert(g_fakeCalls.size() == 2 && !g_fakeCalls[0].begin && g_fakeCalls[1].begin);
	assert(g_fakeCalls[0].self == &gameServer && g_fakeCalls[0].steamId == ID_A);
	assert(g_fakeCalls[1].self == &gameServer && g_fakeCalls[1].steamId == ID_A && g_fakeCalls[1].ticket == Copy(TICKET_A));
	g_fakeCalls.clear();

	// Without the engine interfaces a frame does nothing, and stopping forgets everything.
	SteamReauthFrame();
	assert(g_fakeCalls.empty());
	StopSteamReauth();
	assert(tracker.Empty());
	assert(SteamReauthenticate(ID_A, TICKET_A) == AUTH_BEGIN_UNAVAILABLE && g_fakeCalls.empty());
	setEngineInterface(nullptr);

	// Engine code that differs from the supported build is refused before anything is written.
	const std::array<Byte, 9> foreignBytes = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	CodePatch foreign("foreign edit", site, SteamReauthCallSiteBytes(), foreignBytes);
	foreign.Enable();
	ExpectFailure([&] { PatchManager manager; InstallSteamReauth(manager, module); });
	assert(foreign.Disable());
	assert(ReadMemory(site, original.size()) == original);
	ExpectFailure([&] { StartSteamReauth(nullptr, nullptr); });
	StopSteamReauth();
}
}

void TestSteamReauth(const LoadedModule& module, void* steam3Server)
{
	assert(steam3Server != nullptr);
	TestSchedule();
	TestNetworkIds();
	TestOutcomes();
	TestForgetting();
	TestHook(module, steam3Server);
}
