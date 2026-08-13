#pragma once

#include <cstdint>

namespace ClientAudioUpdatePerformance {

constexpr std::int64_t kUpdatesPerSecond = 60;
constexpr std::int32_t kReducedVoiceBudget = 32;
constexpr double kReduceBelowFramesPerSecond = 100.0;
constexpr double kRestoreAboveFramesPerSecond = 120.0;

enum class VoiceMode : std::uint8_t {
	Full,
	Reduced,
};

struct UpdateSchedule {
	const void* audioDevice = nullptr;
	std::int64_t performanceFrequency = 0;
	std::int64_t nextUpdateTick = 0;
	std::int64_t lastObservedTick = 0;
	bool initialized = false;
};

inline void Reset(UpdateSchedule& schedule) {
	schedule = {};
}

inline bool ShouldRunUpdate(
	UpdateSchedule& schedule,
	const void* audioDevice,
	bool realtime,
	std::int64_t now,
	std::int64_t performanceFrequency) {
	if (!realtime || !audioDevice || now < 0 || performanceFrequency <= 0) {
		Reset(schedule);
		return true;
	}

	std::int64_t interval = performanceFrequency / kUpdatesPerSecond;
	if (interval < 1) interval = 1;

	if (!schedule.initialized || schedule.audioDevice != audioDevice ||
		schedule.performanceFrequency != performanceFrequency ||
		now < schedule.lastObservedTick) {
		schedule.audioDevice = audioDevice;
		schedule.performanceFrequency = performanceFrequency;
		schedule.nextUpdateTick = now + interval;
		schedule.lastObservedTick = now;
		schedule.initialized = true;
		return true;
	}

	schedule.lastObservedTick = now;
	if (now < schedule.nextUpdateTick) return false;

	const std::int64_t elapsedIntervals =
		(now - schedule.nextUpdateTick) / interval + 1;
	schedule.nextUpdateTick += elapsedIntervals * interval;
	return true;
}

inline bool HasSustainedVoicePressure(
	std::uint32_t pressureFrames,
	std::uint32_t totalFrames) {
	return totalFrames != 0 &&
		static_cast<std::uint64_t>(pressureFrames) * 2 >= totalFrames;
}

inline VoiceMode SelectVoiceMode(
	VoiceMode current,
	double averageFramesPerSecond,
	bool sustainedVoicePressure) {
	if (current == VoiceMode::Full && sustainedVoicePressure &&
		averageFramesPerSecond < kReduceBelowFramesPerSecond) {
		return VoiceMode::Reduced;
	}
	if (current == VoiceMode::Reduced &&
		averageFramesPerSecond > kRestoreAboveFramesPerSecond) {
		return VoiceMode::Full;
	}
	return current;
}

inline std::int32_t EffectiveVoiceBudget(
	VoiceMode mode,
	std::int32_t physicalCapacity) {
	if (physicalCapacity <= 0) return 0;
	return mode == VoiceMode::Reduced &&
		physicalCapacity > kReducedVoiceBudget
		? kReducedVoiceBudget
		: physicalCapacity;
}

inline std::uint32_t FirstSelectedVoice(
	std::uint32_t engineFirstSelected,
	std::uint32_t candidateCount,
	std::int32_t effectiveVoiceBudget) {
	if (effectiveVoiceBudget <= 0) return candidateCount;
	const std::uint32_t budget = static_cast<std::uint32_t>(effectiveVoiceBudget);
	const std::uint32_t budgetFirst = candidateCount > budget
		? candidateCount - budget
		: 0;
	const std::uint32_t boundedEngineFirst = engineFirstSelected < candidateCount
		? engineFirstSelected
		: candidateCount;
	return boundedEngineFirst > budgetFirst
		? boundedEngineFirst
		: budgetFirst;
}

}  // namespace ClientAudioUpdatePerformance
