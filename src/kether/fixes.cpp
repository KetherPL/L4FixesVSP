// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixes.h"

#include <array>

namespace l4fixes
{
namespace
{
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
		"No Steam logon kick", Library::Engine,
		"_ZN13CSteam3Server34OnValidateAuthTicketResponseHelperEP11CBaseClient20EAuthSessionResponse",
		0x68, NO_STEAM_LOGON_VERIFY, NO_STEAM_LOGON_PATCH, 7
	}
};
static_assert(DEFINITIONS.size() == KETHER_FIX_COUNT);
}

std::span<const FixDefinition, KETHER_FIX_COUNT> KetherFixDefinitions() noexcept
{
	return DEFINITIONS;
}
}
