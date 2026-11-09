// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixes.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace l4fixes
{
namespace
{
// These Linux i386 definitions are ported from the three SourcePawn gamedata files.
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
		0x6CE, NULL_COMMAND_VERIFY, NULL_COMMAND_PATCH
	},
	FixDefinition{
		"HLTV client-info crash", Library::Engine,
		"_ZN11CBaseClient17ProcessClientInfoEP14CLC_ClientInfo",
		23, HLTV_VERIFY, HLTV_PATCH
	},
	FixDefinition{
		"Boomer ladder", Library::Server,
		"_ZN19CTerrorGameMovement15CheckForLaddersEb",
		0x355, BOOMER_LADDER_VERIFY, BOOMER_LADDER_PATCH
	}
};

// The null-command patch jumps backwards to an existing return path. Ensure its
// relative target stays inside this function before installing the branch.
void ValidateBranch(const FixDefinition& definition, const FunctionSymbol& function)
{
	constexpr Byte JMP_REL32 = 0xE9;
	constexpr std::size_t JMP_REL32_SIZE = 5;
	if (definition.replacement[0] != JMP_REL32)
		return;
	std::int32_t displacement;
	std::memcpy(&displacement, definition.replacement.data() + 1, sizeof(displacement));
	const auto targetOffset = static_cast<std::int64_t>(definition.offset)
		+ static_cast<std::int64_t>(JMP_REL32_SIZE) + displacement;
	if (targetOffset < 0 || static_cast<std::uint64_t>(targetOffset) >= function.size)
		throw std::runtime_error("Patch branch leaves its function: " + std::string(definition.name));
}
}

std::span<const FixDefinition> FixDefinitions() noexcept
{
	return DEFINITIONS;
}

void RegisterFixes(PatchManager& manager, const LoadedModule& server, const LoadedModule& engine)
{
	for (const auto& definition : DEFINITIONS)
	{
		const auto& module = definition.library == Library::Server ? server : engine;
		const auto function = module.ResolveFunction(definition.symbol);
		if (definition.offset > function.size || definition.replacement.size() > function.size - definition.offset)
			throw std::runtime_error("Patch offset exceeds its function: " + std::string(definition.name));
		ValidateBranch(definition, function);
		manager.Register(std::make_unique<CodePatch>(std::string(definition.name),
			function.address + definition.offset, definition.verify, definition.replacement));
	}
}
}
