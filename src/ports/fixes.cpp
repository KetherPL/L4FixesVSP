// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixes.h"

#include <array>

namespace l4fixes
{
namespace
{
// Linux i386 definitions ported from SourcePawn gamedata files.
// Keep byte arrays next to their offsets; a game update requires revalidation.
constexpr std::array<Byte, 1> NULL_COMMAND_VERIFY = { 0xC7 };
constexpr std::array<Byte, 7> NULL_COMMAND_PATCH = { 0xE9, 0x32, 0xFC, 0xFF, 0xFF, 0x90, 0x90 };
constexpr std::array<Byte, 4> HLTV_VERIFY = { 0x0F, 0xB6, 0x46, 0x54 };
constexpr std::array<Byte, 4> HLTV_PATCH = { 0x31, 0xC0, 0x66, 0x90 };
constexpr std::array<Byte, 6> BOOMER_LADDER_VERIFY = { 0x0F, 0x84, 0xC9, 0xFE, 0xFF, 0xFF };
constexpr std::array<Byte, 6> BOOMER_LADDER_PATCH = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

constexpr std::array DEFINITIONS = {
	FixDefinition{
		"Null CUserCmd lag compensation", Library::Server,
		"_ZN23CLagCompensationManager20StartLagCompensationEP11CBasePlayer19LagCompensationTypeRK6VectorRK6QAnglef",
		0x6CE, NULL_COMMAND_VERIFY, NULL_COMMAND_PATCH, 0
	},
	FixDefinition{
		"HLTV client-info crash", Library::Engine,
		"_ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo",
		23, HLTV_VERIFY, HLTV_PATCH, std::nullopt
	},
	FixDefinition{
		"Boomer ladder", Library::Server,
		"_ZN19CTerrorGameMovement15CheckForLaddersEb",
		0x355, BOOMER_LADDER_VERIFY, BOOMER_LADDER_PATCH, std::nullopt
	}
};
static_assert(DEFINITIONS.size() == PORTED_FIX_COUNT);
}

std::span<const FixDefinition, PORTED_FIX_COUNT> PortedFixDefinitions() noexcept
{
	return DEFINITIONS;
}
}
