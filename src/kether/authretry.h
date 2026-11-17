// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../module.h"

#include <functional>
#include <optional>

namespace l4fixes
{
// ISteamGameServer::BeginAuthSession results (EBeginAuthSessionResult) the tracker tells apart.
// 1 (invalid ticket), 3 (invalid version), 4 (game mismatch) and 5 (expired ticket) reject the
// ticket itself, so trying it again cannot help.
inline constexpr int AUTH_BEGIN_OK = 0;
inline constexpr int AUTH_BEGIN_DUPLICATE = 2;
// Not a Steam value: the engine's Steam interface could not be used for this attempt.
inline constexpr int AUTH_BEGIN_UNAVAILABLE = -1;

inline constexpr std::size_t AUTH_MAX_TICKET_SIZE = 4096;
inline constexpr std::size_t AUTH_MAX_TICKETS = 128;
inline constexpr int AUTH_MAX_ATTEMPTS = 30;
// Seconds. The first attempt leaves Steam time to answer the engine's own request; the delay then
// doubles up to the maximum, which spreads the attempts over a little more than two hours.
inline constexpr double AUTH_FIRST_DELAY = 10.0;
inline constexpr double AUTH_MAX_DELAY = 300.0;
// A ticket nobody on the server belongs to (a rejected or departed client) is dropped after this.
inline constexpr double AUTH_FORGET_AFTER = 120.0;

// A connected human client as the engine reports it.
struct AuthClient
{
	int index;
	std::uint64_t steamId;
	bool authenticated;
};

struct AuthTicket
{
	std::uint64_t steamId = 0;
	std::vector<Byte> ticket;
	double lastSeen = 0.0;
	double nextAttempt = 0.0;
	int attempts = 0;
	int lastResult = AUTH_BEGIN_OK;
	bool abandoned = false;
	// The client index the ticket's owner was last seen at (0 = not seen yet).
	int client = 0;
	// When the last attempt was made, and the attempt Steam answered positively (0 = none) with
	// the seconds that answer took.
	double lastAttempt = 0.0;
	int answeredAttempt = 0;
	double answerDelay = 0.0;
};

struct AuthRetryEvent
{
	enum class Kind { Attempt, Validated };
	Kind kind;
	int client;
	std::uint64_t steamId;
	int attempts;
	// Attempt only: the BeginAuthSession result, and the seconds until the next attempt
	// (0 = there will be none).
	int result;
	double retryIn;
	// Validated only: the seconds between the attempt and Steam's answer.
	double answeredAfter = 0.0;
};

// Ends the client's auth session and begins it again with the stored ticket; returns the
// BeginAuthSession result or AUTH_BEGIN_UNAVAILABLE.
using AuthRetryFn = std::function<int(std::uint64_t steamId, std::span<const Byte> ticket)>;

// The account ID (the low 32 bits of the 64-bit Steam ID) in a "STEAM_<universe>:<y>:<z>" network
// ID, the form the engine hands to plugins. Empty for anything else ("BOT", "STEAM_ID_LAN", ...).
std::optional<std::uint32_t> SteamAccountId(std::string_view networkId);

// Keeps the Steam auth ticket of every client the engine accepted and decides when a client Steam
// has not validated gets its ticket submitted again. Steam's answer arrives later through the
// engine's own callback: the engine tells plugins about a positive one (RecordValidation), a
// negative one only shows as the client staying unauthenticated.
class AuthRetryTracker
{
public:
	// Stores the ticket the engine just began an auth session with. A new ticket for a known
	// Steam ID (a reconnect) replaces the old one and starts over. Returns false if it is not kept.
	bool RecordTicket(std::uint64_t steamId, std::span<const Byte> ticket, double now);

	// Retries every unauthenticated client whose attempt is due and forgets tickets without a
	// connected client.
	std::vector<AuthRetryEvent> Update(double now, std::span<const AuthClient> clients, const AuthRetryFn& retry);

	// Retries one client right away, authenticated or not. Empty if no ticket is stored for it.
	std::optional<AuthRetryEvent> Force(const AuthClient& client, double now, const AuthRetryFn& retry);

	// Steam validated the client with this account ID. Returns the event to report if that answers
	// an attempt made here; empty for the engine's own request at connect or a repeated answer.
	std::optional<AuthRetryEvent> RecordValidation(std::uint32_t accountId, double now);

	const AuthTicket* Find(std::uint64_t steamId) const noexcept;
	std::span<const AuthTicket> Tickets() const noexcept { return m_tickets; }
	bool Empty() const noexcept { return m_tickets.empty(); }
	void Clear() noexcept { m_tickets.clear(); }

private:
	AuthTicket* FindTicket(std::uint64_t steamId) noexcept;
	AuthRetryEvent Attempt(AuthTicket& ticket, int client, double now, const AuthRetryFn& retry);

	std::vector<AuthTicket> m_tickets;
};
}
