// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "irecipientfilter.h"

#include <array>
#include <string>

class CCommand;
class IEngineSound;

namespace l4fixes
{
inline constexpr char RELIABLE_SOUND_COMMAND[] = "te_reliable_sound";
inline constexpr int RELIABLE_SOUND_MAX_RECIPIENTS = 64;

// Sounds emitted by the game and by SourceMod are unreliable: they ride in the next snapshot only,
// so a lost packet loses the sound for good. A filter that reports IsReliable() makes the engine
// send each recipient the sound right away on the reliable stream (CGameServer::BroadcastSound ->
// CGameClient::SendSound(sound, true)), which is retransmitted until it arrives.
class ReliableRecipientFilter final : public IRecipientFilter
{
public:
	bool IsReliable() const override { return true; }
	bool IsInitMessage() const override { return false; }
	int GetRecipientCount() const override { return m_count; }
	int GetRecipientIndex(int slot) const override { return slot >= 0 && slot < m_count ? m_clients[slot] : -1; }

	// Adds a client once; returns false if the list is full.
	bool Add(int client);

private:
	std::array<int, RELIABLE_SOUND_MAX_RECIPIENTS> m_clients = {};
	int m_count = 0;
};

// Whether client index 1..maxClients should get sounds: connected humans and SourceTV.
using SoundRecipientFn = bool (*)(int client);

// te_reliable_sound <entity> <channel> <soundlevel> <volume> <pitch> <flags> <x,y,z|-> "<sample>" [clients]
//   entity   sound source: 0 = world, or an entity index
//   origin   "x,y,z", or "-" to use the entity's own position (entity must not be the world)
//   sample   must already be precached; quote it (samples often start with ')', which splits tokens)
//   clients  optional comma separated client indices; default: every recipient client
// The arguments match EmitSound / a SourceMod normal sound hook, so a hooked sound can be re-emitted
// unchanged, only reliably. Clients that aren't recipients are skipped. The sound goes through
// IEngineSound::EmitSound, so SourceMod sound hooks see it too; returning Plugin_Changed from one
// re-emits it unreliably, so callers run the command on the next frame, outside the hook.
// Returns false and sets 'error' if the arguments are invalid; nothing is emitted then.
bool EmitReliableSound(const CCommand& args, IEngineSound& engineSound, int maxClients,
	SoundRecipientFn isSoundRecipient, std::string& error);
}
