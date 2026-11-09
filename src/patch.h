// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "module.h"

#include <memory>

namespace l4fixes
{
std::vector<Byte> ReadMemory(std::uintptr_t address, std::size_t length);

class CodePatch
{
public:
	CodePatch(std::string name, std::uintptr_t address, std::span<const Byte> verify,
		std::span<const Byte> replacement);
	~CodePatch();
	CodePatch(const CodePatch&) = delete;
	CodePatch& operator=(const CodePatch&) = delete;

	void Enable();
	bool Disable() noexcept;
	bool IsExternal() const noexcept { return m_external; }
	const std::string& Name() const noexcept { return m_name; }

private:
	std::string m_name;
	std::uintptr_t m_address;
	std::vector<Byte> m_original;
	std::vector<Byte> m_replacement;
	bool m_external = false;
	bool m_enabled = false;
};

class PatchManager
{
public:
	~PatchManager() { DisableAll(); }
	void Register(std::unique_ptr<CodePatch> patch);
	void EnableAll();
	bool DisableAll() noexcept;
	void Clear() noexcept;
	bool Empty() const noexcept { return m_patches.empty(); }

private:
	std::vector<std::unique_ptr<CodePatch>> m_patches;
};
}
