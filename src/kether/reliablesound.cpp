// SPDX-License-Identifier: GPL-3.0-or-later
#include "reliablesound.h"

#include "engine/IEngineSound.h"
#include "mathlib/vector.h"
#include "tier1/convar.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace l4fixes
{
namespace
{
constexpr long MAX_ENTITY = 2047; // MAX_EDICTS - 1
constexpr long MAX_CHANNEL = 7;   // CHAN_AUTO .. CHAN_VOICE2
constexpr std::size_t MAX_SAMPLE_LENGTH = 255;
constexpr std::size_t MAX_ORIGIN_LENGTH = 127;
constexpr std::size_t MAX_CLIENT_LIST_LENGTH = 511;
constexpr char USAGE[] = "usage: te_reliable_sound <entity> <channel> <soundlevel> <volume> <pitch> <flags>"
	" <x,y,z|-> \"<sample>\" [client,client,...]";

// Whole-token integer in [minValue, maxValue]. strtol keeps the accepted syntax of earlier versions.
bool ParseInt(std::string_view text, long minValue, long maxValue, long& value)
{
	if (text.empty())
		return false;
	const std::string token(text);
	char* end = nullptr;
	errno = 0;
	value = std::strtol(token.c_str(), &end, 10);
	return errno != ERANGE && end == token.c_str() + token.size() && value >= minValue && value <= maxValue;
}

// Whole-token finite float in [minValue, maxValue].
bool ParseFloat(std::string_view text, float minValue, float maxValue, float& value)
{
	if (text.empty())
		return false;
	const std::string token(text);
	char* end = nullptr;
	const double parsed = std::strtod(token.c_str(), &end);
	if (end != token.c_str() + token.size() || !std::isfinite(parsed) || parsed < minValue || parsed > maxValue)
		return false;
	value = static_cast<float>(parsed);
	return true;
}

// "x,y,z"
bool ParseOrigin(std::string_view text, Vector& origin)
{
	if (text.empty() || text.size() > MAX_ORIGIN_LENGTH)
		return false;
	std::array<float, 3> coordinates = {};
	for (std::size_t index = 0; index < coordinates.size(); ++index)
	{
		const auto separator = text.find(',');
		const bool last = index + 1 == coordinates.size();
		if (last != (separator == std::string_view::npos))
			return false;
		if (!ParseFloat(text.substr(0, separator), -1.0e6f, 1.0e6f, coordinates[index]))
			return false;
		text.remove_prefix(last ? text.size() : separator + 1);
	}
	origin.Init(coordinates[0], coordinates[1], coordinates[2]);
	return true;
}

// Printable sample path: no control characters, and not starting with a space or a non-ASCII byte.
bool IsValidSample(std::string_view sample)
{
	if (sample.empty() || sample.size() > MAX_SAMPLE_LENGTH)
		return false;
	const auto first = static_cast<unsigned char>(sample.front());
	if (first <= 0x20 || first >= 0x80)
		return false;
	return std::none_of(sample.begin(), sample.end(), [](char character) {
		const auto value = static_cast<unsigned char>(character);
		return value < 0x20 || value == 0x7F;
	});
}

bool Fail(std::string& error, const char* message)
{
	error = message;
	return false;
}
}

bool ReliableRecipientFilter::Add(int client)
{
	const auto end = m_clients.begin() + m_count;
	if (std::find(m_clients.begin(), end, client) != end)
		return true;
	if (m_count >= RELIABLE_SOUND_MAX_RECIPIENTS)
		return false;
	m_clients[m_count++] = client;
	return true;
}

bool EmitReliableSound(const CCommand& args, IEngineSound& engineSound, int maxClients,
	SoundRecipientFn isSoundRecipient, std::string& error)
{
	error.clear();
	if (args.ArgC() != 9 && args.ArgC() != 10)
		return Fail(error, USAGE);

	long entity = 0, channel = 0, soundLevel = 0, pitch = 0, flags = 0;
	float volume = 0.0f;
	if (!ParseInt(args.Arg(1), 0, MAX_ENTITY, entity))
		return Fail(error, "invalid entity (0 = world, up to 2047)");
	if (!ParseInt(args.Arg(2), 0, MAX_CHANNEL, channel))
		return Fail(error, "invalid channel (0-7)");
	if (!ParseInt(args.Arg(3), 0, 255, soundLevel))
		return Fail(error, "invalid sound level (0-255)");
	if (!ParseFloat(args.Arg(4), 0.0f, 1.0f, volume))
		return Fail(error, "invalid volume (0-1)");
	if (!ParseInt(args.Arg(5), 1, 255, pitch))
		return Fail(error, "invalid pitch (1-255)");
	if (!ParseInt(args.Arg(6), 0, 0x7FFFFFFF, flags))
		return Fail(error, "invalid flags");

	Vector origin;
	const Vector* originPointer = nullptr;
	if (std::string_view(args.Arg(7)) == "-")
	{
		if (entity == 0)
			return Fail(error, "the world has no position of its own, give an origin");
	}
	else if (ParseOrigin(args.Arg(7), origin))
	{
		originPointer = &origin;
	}
	else
	{
		return Fail(error, "invalid origin (x,y,z or -)");
	}

	const char* sample = args.Arg(8);
	if (!IsValidSample(sample))
		return Fail(error, "invalid sample (printable path, quote it)");
	// Precaching during the game can Host_Error the server (full table, bad name); callers precache.
	if (!engineSound.IsSoundPrecached(sample))
		return Fail(error, "sample is not precached (precache it on map start)");

	ReliableRecipientFilter filter;
	if (args.ArgC() == 10)
	{
		std::string_view list(args.Arg(9));
		if (list.empty() || list.size() > MAX_CLIENT_LIST_LENGTH)
			return Fail(error, "invalid client list");
		while (true)
		{
			const auto separator = list.find(',');
			long client = 0;
			if (!ParseInt(list.substr(0, separator), 1, maxClients, client))
				return Fail(error, "invalid client list (client indices 1-maxplayers)");
			if (isSoundRecipient(static_cast<int>(client)))
				filter.Add(static_cast<int>(client));
			if (separator == std::string_view::npos)
				break;
			list.remove_prefix(separator + 1);
		}
	}
	else
	{
		for (int client = 1; client <= maxClients; ++client)
		{
			if (isSoundRecipient(client))
				filter.Add(client);
		}
	}

	if (filter.GetRecipientCount() == 0)
		return true;
	engineSound.EmitSound(filter, static_cast<int>(entity), static_cast<int>(channel), sample, volume,
		static_cast<soundlevel_t>(soundLevel), static_cast<int>(flags), static_cast<int>(pitch),
		originPointer, nullptr, nullptr, true, 0.0f, -1);
	return true;
}
}
