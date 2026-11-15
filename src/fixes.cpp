// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixes.h"
#include "kether/fixes.h"
#include "ports/fixes.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace l4fixes
{
namespace
{
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
	static const auto definitions = []
	{
		std::array<FixDefinition, PORTED_FIX_COUNT + KETHER_FIX_COUNT> combined;
		const auto ported = PortedFixDefinitions();
		const auto kether = KetherFixDefinitions();
		const auto next = std::copy(ported.begin(), ported.end(), combined.begin());
		std::copy(kether.begin(), kether.end(), next);
		return combined;
	}();
	return definitions;
}

void RegisterFixes(PatchManager& manager, const LoadedModule& server, const LoadedModule& engine)
{
	for (const auto& definition : FixDefinitions())
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
