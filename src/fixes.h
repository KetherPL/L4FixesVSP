// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "patch.h"

#include <optional>

namespace l4fixes
{
enum class Library { Server, Engine };

struct FixDefinition
{
	std::string_view name;
	Library library;
	std::string_view symbol;
	std::size_t offset;
	std::span<const Byte> verify;
	std::span<const Byte> replacement;
	// Position of a relative jump (E9 rel32 or EB rel8) inside the replacement, if it installs one.
	// Its target must stay inside the patched function.
	std::optional<std::size_t> branch;
};

std::span<const FixDefinition> FixDefinitions() noexcept;
void RegisterFixes(PatchManager& manager, const LoadedModule& server, const LoadedModule& engine);
}
