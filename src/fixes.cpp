// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixes.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace l4fixes
{
namespace
{
// The first three Linux i386 definitions are ported from SourcePawn gamedata files.
// Keep byte arrays next to their offsets; a game update requires revalidation.
constexpr std::array<Byte, 1> NULL_COMMAND_VERIFY = { 0xC7 };
constexpr std::array<Byte, 7> NULL_COMMAND_PATCH = { 0xE9, 0x32, 0xFC, 0xFF, 0xFF, 0x90, 0x90 };
constexpr std::array<Byte, 4> HLTV_VERIFY = { 0x0F, 0xB6, 0x46, 0x54 };
constexpr std::array<Byte, 4> HLTV_PATCH = { 0x31, 0xC0, 0x66, 0x90 };
constexpr std::array<Byte, 6> BOOMER_LADDER_VERIFY = { 0x0F, 0x84, 0xC9, 0xFE, 0xFF, 0xFF };
constexpr std::array<Byte, 6> BOOMER_LADDER_PATCH = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
// CSteam3Server::OnValidateAuthTicketResponseHelper kicks the client of a failed Steam auth response.
// Responses 1, 6, 7 and 8 share one branch of its switch, which ends with "No Steam logon":
//   83 BF 98 00 00 00 01   cmp dword ptr [edi+98h], 1   server runs without Steam authentication?
//   74 1F                  je  <return>                 then the engine already keeps the client
// je -> jmp takes that existing path on every server. The engine still prints and logs
// "STEAMAUTH: Client %s received failure code %d" before the switch. The other responses (no
// license, VAC ban, account in use elsewhere, timeout) keep their kicks; their branches differ.
constexpr std::array<Byte, 9> NO_STEAM_LOGON_VERIFY = { 0x83, 0xBF, 0x98, 0x00, 0x00, 0x00, 0x01, 0x74, 0x1F };
constexpr std::array<Byte, 9> NO_STEAM_LOGON_PATCH = { 0x83, 0xBF, 0x98, 0x00, 0x00, 0x00, 0x01, 0xEB, 0x1F };

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
	},
	FixDefinition{
		"No Steam logon kick", Library::Engine,
		"_ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse",
		0x68, NO_STEAM_LOGON_VERIFY, NO_STEAM_LOGON_PATCH, 7
	}
};

// The null-command patch jumps backwards to an existing return path, the "No Steam logon" patch
// forwards to one. Ensure a relative target stays inside its function before installing the branch.
void ValidateBranch(const FixDefinition& definition, const FunctionSymbol& function)
{
	constexpr Byte JMP_REL32 = 0xE9;
	constexpr Byte JMP_REL8 = 0xEB;
	if (!definition.branch)
		return;
	const auto position = *definition.branch;
	const auto& code = definition.replacement;
	std::int64_t displacement = 0;
	std::size_t length = 0;
	if (position < code.size() && code[position] == JMP_REL32 && code.size() - position >= 5)
	{
		std::int32_t value;
		std::memcpy(&value, code.data() + position + 1, sizeof(value));
		displacement = value;
		length = 5;
	}
	else if (position < code.size() && code[position] == JMP_REL8 && code.size() - position >= 2)
	{
		displacement = static_cast<std::int8_t>(code[position + 1]);
		length = 2;
	}
	else
	{
		throw std::runtime_error("Patch branch is not a relative jump: " + std::string(definition.name));
	}
	const auto targetOffset = static_cast<std::int64_t>(definition.offset)
		+ static_cast<std::int64_t>(position + length) + displacement;
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
