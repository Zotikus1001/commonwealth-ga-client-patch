#include "src/ClientPatches/PerformanceOptimizations/Audio/AudioUpdatePolicy.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace ClientAudioUpdatePerformance;

int main() {
	char firstDevice = 0;
	char secondDevice = 0;
	UpdateSchedule schedule{};

	assert(ShouldRunUpdate(schedule, &firstDevice, true, 0, 60000));
	assert(!ShouldRunUpdate(schedule, &firstDevice, true, 999, 60000));
	assert(ShouldRunUpdate(schedule, &firstDevice, true, 1000, 60000));
	assert(ShouldRunUpdate(schedule, &secondDevice, true, 1001, 60000));
	assert(!ShouldRunUpdate(schedule, &secondDevice, true, 1500, 60000));
	assert(ShouldRunUpdate(schedule, &secondDevice, false, 1500, 60000));
	assert(ShouldRunUpdate(schedule, &secondDevice, true, 1501, 60000));
	assert(ShouldRunUpdate(schedule, &secondDevice, true, 100, 60000));
	assert(!ShouldRunUpdate(schedule, &secondDevice, true, 1099, 60000));
	assert(ShouldRunUpdate(schedule, &secondDevice, true, 1100, 60000));
	assert(ShouldRunUpdate(schedule, nullptr, true, 1101, 60000));
	assert(ShouldRunUpdate(schedule, &firstDevice, true, 1102, 0));

	UpdateSchedule highFrameRate{};
	std::uint32_t updates = 0;
	for (std::int64_t tick = 0; tick < 240000; tick += 1000) {
		if (ShouldRunUpdate(highFrameRate, &firstDevice, true, tick, 240000)) {
			++updates;
		}
	}
	assert(updates == 60);

	UpdateSchedule lowFrameRate{};
	for (std::int64_t tick = 0; tick < 60000; tick += 2000) {
		assert(ShouldRunUpdate(lowFrameRate, &firstDevice, true, tick, 60000));
	}

	assert(!HasSustainedVoicePressure(49, 100));
	assert(HasSustainedVoicePressure(50, 100));
	assert(SelectVoiceMode(VoiceMode::Full, 99.9, true) == VoiceMode::Reduced);
	assert(SelectVoiceMode(VoiceMode::Full, 100.0, true) == VoiceMode::Full);
	assert(SelectVoiceMode(VoiceMode::Full, 60.0, false) == VoiceMode::Full);
	assert(SelectVoiceMode(VoiceMode::Reduced, 120.0, true) == VoiceMode::Reduced);
	assert(SelectVoiceMode(VoiceMode::Reduced, 120.1, true) == VoiceMode::Full);
	assert(SelectVoiceMode(VoiceMode::Reduced, 110.0, false) == VoiceMode::Reduced);

	assert(EffectiveVoiceBudget(VoiceMode::Full, 64) == 64);
	assert(EffectiveVoiceBudget(VoiceMode::Reduced, 64) == 32);
	assert(EffectiveVoiceBudget(VoiceMode::Reduced, 24) == 24);
	assert(EffectiveVoiceBudget(VoiceMode::Reduced, -1) == 0);
	assert(FirstSelectedVoice(12, 80, 32) == 48);
	assert(FirstSelectedVoice(60, 80, 32) == 60);
	assert(FirstSelectedVoice(0, 20, 32) == 0);
	assert(FirstSelectedVoice(0, 20, 0) == 20);
	assert(FirstSelectedVoice(100, 80, 32) == 80);

	std::puts("audio update performance tests passed");
	return 0;
}
