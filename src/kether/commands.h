// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "interface.h"

class IVEngineServer;

namespace l4fixes
{
class KetherCommands
{
public:
	void Register(CreateInterfaceFn engineFactory, CreateInterfaceFn serverFactory, IVEngineServer* engine);
	void Unregister() noexcept;

private:
	bool m_commandsRegistered = false;
};
}
