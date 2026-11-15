// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../fixes.h"

namespace l4fixes
{
inline constexpr std::size_t KETHER_FIX_COUNT = 1;

std::span<const FixDefinition, KETHER_FIX_COUNT> KetherFixDefinitions() noexcept;
}
