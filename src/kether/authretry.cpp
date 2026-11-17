// SPDX-License-Identifier: GPL-3.0-or-later
#include "authretry.h"

#include <algorithm>
#include <charconv>

namespace l4fixes
{
namespace
{
bool RejectsTicket(int result)
{
	return result != AUTH_BEGIN_OK && result != AUTH_BEGIN_DUPLICATE && result != AUTH_BEGIN_UNAVAILABLE;
}

// The whole of 'text' as a decimal number.
bool ParseNumber(std::string_view text, std::uint32_t& value)
{
	const char* end = text.data() + text.size();
	const auto parsed = std::from_chars(text.data(), end, value);
	return !text.empty() && parsed.ec == std::errc() && parsed.ptr == end;
}
}

std::optional<std::uint32_t> SteamAccountId(std::string_view networkId)
{
	constexpr std::string_view prefix = "STEAM_";
	if (!networkId.starts_with(prefix))
		return std::nullopt;
	networkId.remove_prefix(prefix.size());
	const auto first = networkId.find(':');
	const auto second = first != std::string_view::npos ? networkId.find(':', first + 1) : first;
	if (second == std::string_view::npos)
		return std::nullopt;
	std::uint32_t universe = 0;
	std::uint32_t low = 0;
	std::uint32_t high = 0;
	if (!ParseNumber(networkId.substr(0, first), universe)
		|| !ParseNumber(networkId.substr(first + 1, second - first - 1), low)
		|| !ParseNumber(networkId.substr(second + 1), high) || low > 1 || high > 0x7FFFFFFFu)
		return std::nullopt;
	return high << 1 | low;
}

AuthTicket* AuthRetryTracker::FindTicket(std::uint64_t steamId) noexcept
{
	const auto found = std::find_if(m_tickets.begin(), m_tickets.end(),
		[steamId](const AuthTicket& ticket) { return ticket.steamId == steamId; });
	return found != m_tickets.end() ? &*found : nullptr;
}

const AuthTicket* AuthRetryTracker::Find(std::uint64_t steamId) const noexcept
{
	return const_cast<AuthRetryTracker*>(this)->FindTicket(steamId);
}

bool AuthRetryTracker::RecordTicket(std::uint64_t steamId, std::span<const Byte> ticket, double now)
{
	if (steamId == 0 || ticket.empty() || ticket.size() > AUTH_MAX_TICKET_SIZE)
		return false;
	AuthTicket* entry = FindTicket(steamId);
	if (entry == nullptr)
	{
		if (m_tickets.size() >= AUTH_MAX_TICKETS)
		{
			// Full of tickets nobody has claimed yet: replace the one unseen for the longest.
			entry = &*std::min_element(m_tickets.begin(), m_tickets.end(),
				[](const AuthTicket& left, const AuthTicket& right) { return left.lastSeen < right.lastSeen; });
		}
		else
		{
			entry = &m_tickets.emplace_back();
		}
	}
	*entry = AuthTicket{};
	entry->steamId = steamId;
	entry->ticket.assign(ticket.begin(), ticket.end());
	entry->lastSeen = now;
	entry->nextAttempt = now + AUTH_FIRST_DELAY;
	return true;
}

AuthRetryEvent AuthRetryTracker::Attempt(AuthTicket& ticket, int client, double now, const AuthRetryFn& retry)
{
	const int result = retry(ticket.steamId, ticket.ticket);
	++ticket.attempts;
	ticket.lastResult = result;
	ticket.lastAttempt = now;
	double delay = AUTH_FIRST_DELAY;
	for (int attempt = 0; attempt < ticket.attempts && delay < AUTH_MAX_DELAY; ++attempt)
		delay *= 2.0;
	delay = std::min(delay, AUTH_MAX_DELAY);
	ticket.nextAttempt = now + delay;
	if (RejectsTicket(result) || ticket.attempts >= AUTH_MAX_ATTEMPTS)
		ticket.abandoned = true;
	return { AuthRetryEvent::Kind::Attempt, client, ticket.steamId, ticket.attempts, result,
		ticket.abandoned ? 0.0 : delay };
}

std::vector<AuthRetryEvent> AuthRetryTracker::Update(double now, std::span<const AuthClient> clients,
	const AuthRetryFn& retry)
{
	std::vector<AuthRetryEvent> events;
	for (const auto& client : clients)
	{
		AuthTicket* ticket = FindTicket(client.steamId);
		if (ticket == nullptr)
			continue;
		ticket->lastSeen = now;
		ticket->client = client.index;
		if (!client.authenticated && !ticket->abandoned && now >= ticket->nextAttempt)
			events.push_back(Attempt(*ticket, client.index, now, retry));
	}
	std::erase_if(m_tickets, [now](const AuthTicket& ticket) { return now - ticket.lastSeen > AUTH_FORGET_AFTER; });
	return events;
}

std::optional<AuthRetryEvent> AuthRetryTracker::Force(const AuthClient& client, double now, const AuthRetryFn& retry)
{
	AuthTicket* ticket = FindTicket(client.steamId);
	if (ticket == nullptr)
		return std::nullopt;
	ticket->lastSeen = now;
	ticket->client = client.index;
	AuthRetryEvent event = Attempt(*ticket, client.index, now, retry);
	// Update leaves an authenticated client alone, so nothing follows this attempt.
	if (client.authenticated)
		event.retryIn = 0.0;
	return event;
}

std::optional<AuthRetryEvent> AuthRetryTracker::RecordValidation(std::uint32_t accountId, double now)
{
	const auto found = std::find_if(m_tickets.begin(), m_tickets.end(),
		[accountId](const AuthTicket& ticket) { return static_cast<std::uint32_t>(ticket.steamId) == accountId; });
	if (found == m_tickets.end() || found->attempts == 0 || found->answeredAttempt == found->attempts)
		return std::nullopt;
	found->answeredAttempt = found->attempts;
	found->answerDelay = now - found->lastAttempt;
	return AuthRetryEvent{ AuthRetryEvent::Kind::Validated, found->client, found->steamId, found->attempts,
		AUTH_BEGIN_OK, 0.0, found->answerDelay };
}
}
