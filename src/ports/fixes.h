// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../fixes.h"

namespace l4fixes
{
inline constexpr std::size_t PORTED_FIX_COUNT = 3;

std::span<const FixDefinition, PORTED_FIX_COUNT> PortedFixDefinitions() noexcept;
}
