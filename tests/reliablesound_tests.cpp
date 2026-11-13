// SPDX-License-Identifier: GPL-3.0-or-later
#include "reliablesound.h"

#include "engine/IEngineSound.h"
#include "mathlib/vector.h"
#include "tier1/convar.h"

#include <cassert>
#include <string>
#include <vector>

#ifdef NDEBUG
#error Regression tests require assertions.
#endif

using namespace l4fixes;

namespace
{
struct EmittedSound
{
	bool reliable = false;
	bool initMessage = false;
	std::vector<int> recipients;
	int entity = 0;
	int channel = 0;
	std::string sample;
	float volume = 0.0f;
	int soundLevel = 0;
	int flags = 0;
	int pitch = 0;
	bool hasOrigin = false;
	Vector origin;
};

class FakeEngineSound final : public IEngineSound
{
public:
	std::vector<EmittedSound> emitted;
	std::vector<std::string> precached;
	int precacheCalls = 0;

	bool PrecacheSound(const char*, bool, bool) override { ++precacheCalls; return true; }
	bool IsSoundPrecached(const char* sample) override
	{
		for (const auto& name : precached)
		{
			if (name == sample)
				return true;
		}
		return false;
	}
	void PrefetchSound(const char*) override {}
	bool IsLoopingSound(const char*) override { return false; }
	float GetSoundDuration(const char*) override { return 0.0f; }
	void EmitSound(IRecipientFilter&, int, int, const char*, float, float, int, int, const Vector*, const Vector*,
		CUtlVector<Vector>*, bool, float, int) override
	{
		assert(!"the soundlevel_t overload must be used");
	}
	void EmitSound(IRecipientFilter& filter, int entity, int channel, const char* sample, float volume, soundlevel_t soundLevel,
		int flags, int pitch, const Vector* origin, const Vector* direction, CUtlVector<Vector>* origins, bool updatePositions,
		float soundTime, int speakerEntity) override
	{
		assert(direction == nullptr && origins == nullptr && updatePositions && soundTime == 0.0f && speakerEntity == -1);
		EmittedSound sound;
		sound.reliable = filter.IsReliable();
		sound.initMessage = filter.IsInitMessage();
		for (int slot = 0; slot < filter.GetRecipientCount(); ++slot)
			sound.recipients.push_back(filter.GetRecipientIndex(slot));
		assert(filter.GetRecipientIndex(-1) == -1 && filter.GetRecipientIndex(filter.GetRecipientCount()) == -1);
		sound.entity = entity;
		sound.channel = channel;
		sound.sample = sample;
		sound.volume = volume;
		sound.soundLevel = static_cast<int>(soundLevel);
		sound.flags = flags;
		sound.pitch = pitch;
		sound.hasOrigin = origin != nullptr;
		if (origin != nullptr)
			sound.origin = *origin;
		emitted.push_back(sound);
	}
	void EmitSentenceByIndex(IRecipientFilter&, int, int, int, float, soundlevel_t, int, int, const Vector*, const Vector*,
		CUtlVector<Vector>*, bool, float, int) override {}
	void StopSound(int, int, const char*) override {}
	void StopAllSounds(bool) override {}
	void SetRoomType(IRecipientFilter&, int) override {}
	void SetPlayerDSP(IRecipientFilter&, int, bool) override {}
	void EmitAmbientSound(const char*, float, int, int, float) override {}
	float GetDistGainFromSoundLevel(soundlevel_t, float) override { return 0.0f; }
	int GetGuidForLastSoundEmitted() override { return 0; }
	bool IsSoundStillPlaying(int) override { return false; }
	void StopSoundByGuid(int) override {}
	void SetVolumeByGuid(int, float) override {}
	void GetActiveSounds(CUtlVector<SndInfo_t>&) override {}
	void PrecacheSentenceGroup(const char*) override {}
	void NotifyBeginMoviePlayback() override {}
	void NotifyEndMoviePlayback() override {}
	bool GetSoundChannelVolume(const char*, float&, float&) override { return false; }
	float GetElapsedTimeByGuid(int) override { return 0.0f; }
};

constexpr int MAX_CLIENTS = 8;

// Clients 1, 2, 4 and 8 are connected humans, 7 is SourceTV; 3 is a bot, the rest are free.
bool IsRecipient(int client)
{
	assert(client >= 1 && client <= MAX_CLIENTS);
	return client == 1 || client == 2 || client == 4 || client == 7 || client == 8;
}

bool Run(FakeEngineSound& engine, const char* commandLine, std::string& error)
{
	CCommand args;
	assert(args.Tokenize(commandLine));
	const bool emitted = EmitReliableSound(args, engine, MAX_CLIENTS, IsRecipient, error);
	assert(emitted == error.empty());
	return emitted;
}

bool Run(FakeEngineSound& engine, const char* commandLine)
{
	std::string error;
	return Run(engine, commandLine, error);
}

void ExpectInvalid(const char* commandLine)
{
	FakeEngineSound engine;
	engine.precached.push_back("a.wav");
	std::string error;
	assert(!Run(engine, commandLine, error));
	assert(engine.emitted.empty() && engine.precacheCalls == 0);
}

void TestWorldSoundToEveryone()
{
	FakeEngineSound engine;
	engine.precached.push_back("player/jockey/voice/alert/jockey_02.wav");
	assert(Run(engine, "te_reliable_sound 0 6 95 0.75 105 0 1.5,-2,300 \"player/jockey/voice/alert/jockey_02.wav\""));
	assert(engine.emitted.size() == 1);
	const auto& sound = engine.emitted[0];
	assert(sound.reliable && !sound.initMessage);
	assert(sound.recipients == std::vector<int>({ 1, 2, 4, 7, 8 })); // humans and SourceTV
	assert(sound.entity == 0 && sound.channel == 6 && sound.soundLevel == 95 && sound.pitch == 105 && sound.flags == 0);
	assert(sound.volume == 0.75f);
	assert(sound.sample == "player/jockey/voice/alert/jockey_02.wav");
	assert(sound.hasOrigin && sound.origin.x == 1.5f && sound.origin.y == -2.0f && sound.origin.z == 300.0f);
	assert(engine.precacheCalls == 0); // never precached at runtime
}

// Precaching during the game can stop the map, so an unknown sample is refused.
void TestNotPrecached()
{
	FakeEngineSound engine;
	std::string error;
	assert(!Run(engine, "te_reliable_sound 0 6 95 1 100 0 0,0,0 \"player/missing.wav\"", error));
	assert(error.find("precached") != std::string::npos);
	assert(engine.emitted.empty() && engine.precacheCalls == 0);
}

// A hooked game sound re-emitted unchanged: same entity, channel and recipients, entity position.
void TestRelayToHookRecipients()
{
	FakeEngineSound engine;
	engine.precached.push_back(")player/charger/voice/idle/Charger_lurk_01.wav");
	assert(Run(engine, "te_reliable_sound 5 2 85 1 100 1024 - \")player/charger/voice/idle/Charger_lurk_01.wav\" 1,3,4,4,7"));
	assert(engine.emitted.size() == 1 && engine.precacheCalls == 0);
	const auto& sound = engine.emitted[0];
	assert(sound.reliable);
	assert(sound.recipients == std::vector<int>({ 1, 4, 7 })); // bot 3 skipped, duplicate 4 once, SourceTV kept
	assert(sound.entity == 5 && sound.channel == 2 && sound.soundLevel == 85 && sound.flags == 1024);
	assert(!sound.hasOrigin);
	assert(sound.sample == ")player/charger/voice/idle/Charger_lurk_01.wav");
}

void TestNoRecipients()
{
	FakeEngineSound engine;
	engine.precached.push_back("a.wav");
	assert(Run(engine, "te_reliable_sound 5 2 85 1 100 0 - \"a.wav\" 3,5"));
	assert(engine.emitted.empty() && engine.precacheCalls == 0);
}

void TestRecipientFilter()
{
	ReliableRecipientFilter filter;
	for (int client = 1; client <= RELIABLE_SOUND_MAX_RECIPIENTS; ++client)
		assert(filter.Add(client));
	assert(filter.Add(1)); // already listed
	assert(!filter.Add(RELIABLE_SOUND_MAX_RECIPIENTS + 1));
	assert(filter.GetRecipientCount() == RELIABLE_SOUND_MAX_RECIPIENTS);
}

void TestInvalid()
{
	ExpectInvalid("te_reliable_sound");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 a.wav 1 extra");
	ExpectInvalid("te_reliable_sound -1 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 2048 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 1x 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 8 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 256 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1.5 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 nan 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 0 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 256 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 -1 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 a,b,c a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,inf a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 - a.wav"); // the world needs an origin
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \"\"");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 0");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 9"); // above maxplayers
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 1,,2");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 1,");
	// strtol overflow must not saturate into a valid value
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 4294967295 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 99999999999999 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 99999999999999 6 95 1 100 0 0,0,0 a.wav");
	ExpectInvalid("te_reliable_sound 5 6 95 1 100 0 - a.wav 99999999999999");
	// samples the engine would choke on
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \" a.wav\"");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \"a\x01.wav\"");
	ExpectInvalid("te_reliable_sound 0 6 95 1 100 0 0,0,0 \"\xC3\xA9.wav\"");
}
}

void TestReliableSound()
{
	TestWorldSoundToEveryone();
	TestNotPrecached();
	TestRelayToHookRecipients();
	TestNoRecipients();
	TestRecipientFilter();
	TestInvalid();
}
