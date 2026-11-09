// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace l4fixes
{
using Byte = std::uint8_t;

struct FunctionSymbol
{
	std::uintptr_t address;
	std::size_t size;
};

// ELF lookup is kept independent of the SDK so it can be exercised in unit tests.
// Private game functions are in .symtab, so dlsym alone is insufficient.
FunctionSymbol FindElfFunction(std::span<const Byte> image, std::string_view name);

class LoadedModule
{
public:
	explicit LoadedModule(const void* anchor);
	FunctionSymbol ResolveFunction(std::string_view name) const;
	bool ContainsCode(std::uintptr_t address, std::size_t length) const noexcept;
	const std::string& Path() const noexcept { return m_path; }

private:
	struct Segment
	{
		std::uintptr_t address;
		std::size_t length;
	};
	std::string m_path;
	std::uintptr_t m_baseAddress = 0;
	std::vector<Segment> m_codeSegments;
	std::vector<Byte> m_image;
};
}
