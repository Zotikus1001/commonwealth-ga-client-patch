#include "src/ClientPatches/PerformanceOptimizations/Audio/AudioUpdatePerformancePatch.hpp"

#include "src/ClientPatches/PerformanceOptimizations/Audio/AudioUpdatePolicy.hpp"
#include "src/Utils/HookBase.hpp"

#include <algorithm>

#ifdef GA_CLIENT_DEBUG
#include "src/Utils/Logger/Logger.hpp"
#endif

namespace {

using ClientAudioUpdatePerformance::VoiceMode;

constexpr std::int64_t kVoiceWindowSeconds = 5;
constexpr std::int64_t kWindowResetGapSeconds = 2;

#ifdef GA_CLIENT_DEBUG
constexpr double kSlowUpdateMilliseconds = 2.0;
#endif

struct AudioDeviceView {
	std::byte unknown00[0x40];
	std::int32_t sourceCapacity;
	std::byte unknown44[0x18];
	std::int32_t audioComponentCount;
	std::byte unknown60[0x14];
	std::int32_t freeSourceCount;
};

struct WaveInstanceArrayView {
	void* data;
	std::int32_t count;
	std::int32_t capacity;
};

static_assert(offsetof(AudioDeviceView, sourceCapacity) == 0x40);
static_assert(offsetof(AudioDeviceView, audioComponentCount) == 0x5C);
static_assert(offsetof(AudioDeviceView, freeSourceCount) == 0x74);
static_assert(sizeof(AudioDeviceView) == 0x78);
static_assert(offsetof(WaveInstanceArrayView, count) == 0x04);
static_assert(sizeof(WaveInstanceArrayView) == 0x0C);

struct VoiceWindow {
	const void* audioDevice = nullptr;
	std::int32_t physicalCapacity = 0;
	VoiceMode mode = VoiceMode::Full;
	std::int64_t startTick = 0;
	std::int64_t lastFrameTick = 0;
	std::uint32_t frames = 0;
	std::uint32_t pressureFrames = 0;
};

LARGE_INTEGER PerformanceFrequency() {
	static const LARGE_INTEGER frequency = [] {
		LARGE_INTEGER value{};
		QueryPerformanceFrequency(&value);
		return value;
	}();
	return frequency;
}

thread_local ClientAudioUpdatePerformance::UpdateSchedule g_updateSchedule{};
thread_local VoiceWindow g_voiceWindow{};
thread_local std::int32_t g_effectiveVoiceBudget = 0;

void ResetVoiceWindow(
	const void* audioDevice,
	std::int32_t physicalCapacity,
	std::int64_t now) {
	g_voiceWindow = {};
	g_voiceWindow.audioDevice = audioDevice;
	g_voiceWindow.physicalCapacity = physicalCapacity;
	g_voiceWindow.startTick = now;
	g_voiceWindow.lastFrameTick = now;
	g_effectiveVoiceBudget = physicalCapacity;
}

void UpdateVoiceBudget(void* audioDevice, int realtime, std::int64_t now) {
	const LARGE_INTEGER frequency = PerformanceFrequency();
	const auto* device = static_cast<const AudioDeviceView*>(audioDevice);
	const std::int32_t physicalCapacity = std::max(0, device->sourceCapacity);
	const std::int32_t freeSources = std::clamp(
		device->freeSourceCount,
		0,
		physicalCapacity);
	const std::int32_t activeSources = physicalCapacity - freeSources;

	if (!realtime || frequency.QuadPart <= 0 ||
		g_voiceWindow.audioDevice != audioDevice ||
		g_voiceWindow.physicalCapacity != physicalCapacity ||
		now < g_voiceWindow.lastFrameTick ||
		now - g_voiceWindow.lastFrameTick >
			frequency.QuadPart * kWindowResetGapSeconds) {
		ResetVoiceWindow(audioDevice, physicalCapacity, now);
	}

	g_voiceWindow.lastFrameTick = now;
	++g_voiceWindow.frames;
	if (activeSources > ClientAudioUpdatePerformance::kReducedVoiceBudget) {
		++g_voiceWindow.pressureFrames;
	}

	const std::int64_t elapsed = now - g_voiceWindow.startTick;
	if (elapsed >= frequency.QuadPart * kVoiceWindowSeconds && elapsed > 0) {
		const double averageFramesPerSecond =
			static_cast<double>(g_voiceWindow.frames) *
			static_cast<double>(frequency.QuadPart) /
			static_cast<double>(elapsed);
		const bool sustainedPressure =
			ClientAudioUpdatePerformance::HasSustainedVoicePressure(
				g_voiceWindow.pressureFrames,
				g_voiceWindow.frames);
		const VoiceMode previousMode = g_voiceWindow.mode;
		g_voiceWindow.mode = ClientAudioUpdatePerformance::SelectVoiceMode(
			previousMode,
			averageFramesPerSecond,
			sustainedPressure);

#ifdef GA_CLIENT_DEBUG
		if (g_voiceWindow.mode != previousMode) {
			Logger::Log(
				"audioprofile",
				"[mode] voice budget %s: five-second-fps=%.1f "
				"physical-sources=%d pressure-frames=%u/%u\n",
				g_voiceWindow.mode == VoiceMode::Reduced
					? "reduced to 32"
					: "restored",
				averageFramesPerSecond,
				physicalCapacity,
				g_voiceWindow.pressureFrames,
				g_voiceWindow.frames);
		}
#endif

		g_voiceWindow.startTick = now;
		g_voiceWindow.frames = 0;
		g_voiceWindow.pressureFrames = 0;
	}

	g_effectiveVoiceBudget = ClientAudioUpdatePerformance::EffectiveVoiceBudget(
		g_voiceWindow.mode,
		physicalCapacity);
}

#ifdef GA_CLIENT_DEBUG

struct ProfileWindow {
	std::uint32_t frames = 0;
	std::uint32_t executedUpdates = 0;
	std::uint32_t skippedUpdates = 0;
	std::uint32_t slowFrames = 0;
	std::uint64_t audioComponentTotal = 0;
	std::uint32_t audioComponentMaximum = 0;
	std::uint64_t activeSourceTotal = 0;
	std::uint32_t activeSourceMaximum = 0;
	double commonTotalMilliseconds = 0.0;
	double commonMaximumMilliseconds = 0.0;
	double gatherTotalMilliseconds = 0.0;
	double gatherMaximumMilliseconds = 0.0;
	double stopTotalMilliseconds = 0.0;
	double stopMaximumMilliseconds = 0.0;
	double startTotalMilliseconds = 0.0;
	double startMaximumMilliseconds = 0.0;
	double backendTotalMilliseconds = 0.0;
	double backendMaximumMilliseconds = 0.0;
};

double ElapsedMilliseconds(
	const LARGE_INTEGER& start,
	const LARGE_INTEGER& end) {
	const LARGE_INTEGER frequency = PerformanceFrequency();
	return frequency.QuadPart > 0
		? static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
			static_cast<double>(frequency.QuadPart)
		: 0.0;
}

thread_local double g_lastCommonMilliseconds = 0.0;
thread_local double g_lastGatherMilliseconds = 0.0;
thread_local double g_lastStopMilliseconds = 0.0;
thread_local double g_lastStartMilliseconds = 0.0;
thread_local bool g_commonMeasuredThisFrame = false;
thread_local bool g_commonSkippedThisFrame = false;
thread_local bool g_insideCommonUpdate = false;
thread_local std::uint32_t g_lastAudioComponentCount = 0;
thread_local std::uint32_t g_lastActiveSourceCount = 0;

#endif

class GatherWaveInstancesHook : public HookBase<
	std::uint32_t(__fastcall*)(void*, void*, void*, int),
	// Builds and sorts the candidate wave-instance array for this update.
	0x10C3F1F0,
	GatherWaveInstancesHook> {
public:
	static std::uint32_t __fastcall Call(
		void* audioDevice,
		void* edx,
		void* waveInstances,
		int realtime) {
#ifdef GA_CLIENT_DEBUG
		LARGE_INTEGER start{};
		LARGE_INTEGER end{};
		QueryPerformanceCounter(&start);
#endif
		const std::uint32_t engineFirstSelected = m_original(
			audioDevice,
			edx,
			waveInstances,
			realtime);
#ifdef GA_CLIENT_DEBUG
		QueryPerformanceCounter(&end);
		if (g_insideCommonUpdate) {
			g_lastGatherMilliseconds += ElapsedMilliseconds(start, end);
		}
#endif
		const auto* instances = static_cast<const WaveInstanceArrayView*>(
			waveInstances);
		const std::uint32_t candidateCount = static_cast<std::uint32_t>(
			std::clamp(instances->count, 0, std::max(0, instances->capacity)));
		return ClientAudioUpdatePerformance::FirstSelectedVoice(
			engineFirstSelected,
			candidateCount,
			g_effectiveVoiceBudget);
	}
};

#ifdef GA_CLIENT_DEBUG

class StopSourcesHook : public HookBase<
	void(__fastcall*)(void*, void*, void*, std::uint32_t),
	// Stops or retains existing sources after candidate prioritization.
	0x10C3BB80,
	StopSourcesHook> {
public:
	static void __fastcall Call(
		void* audioDevice,
		void* edx,
		void* waveInstances,
		std::uint32_t firstSelected) {
		LARGE_INTEGER start{};
		LARGE_INTEGER end{};
		QueryPerformanceCounter(&start);
		m_original(audioDevice, edx, waveInstances, firstSelected);
		QueryPerformanceCounter(&end);
		if (g_insideCommonUpdate) {
			g_lastStopMilliseconds += ElapsedMilliseconds(start, end);
		}
	}
};

class StartSourcesHook : public HookBase<
	void(__fastcall*)(void*, void*, void*, std::uint32_t, int),
	// Starts new sources and updates retained sources for this update.
	0x10C3CAB0,
	StartSourcesHook> {
public:
	static void __fastcall Call(
		void* audioDevice,
		void* edx,
		void* waveInstances,
		std::uint32_t firstSelected,
		int realtime) {
		LARGE_INTEGER start{};
		LARGE_INTEGER end{};
		QueryPerformanceCounter(&start);
		m_original(audioDevice, edx, waveInstances, firstSelected, realtime);
		QueryPerformanceCounter(&end);
		if (g_insideCommonUpdate) {
			g_lastStartMilliseconds += ElapsedMilliseconds(start, end);
		}
	}
};

#endif

class CommonAudioUpdateHook : public HookBase<
	void(__fastcall*)(void*, void*, int),
	// UAudioDevice::Update performs sound-cue/source selection synchronously.
	0x10C3F5C0,
	CommonAudioUpdateHook> {
public:
	static void __fastcall Call(
		void* audioDevice,
		void* edx,
		int realtime) {
		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);
		UpdateVoiceBudget(audioDevice, realtime, now.QuadPart);

		if (!realtime) {
			ClientAudioUpdatePerformance::Reset(g_updateSchedule);
		} else {
			if (!ClientAudioUpdatePerformance::ShouldRunUpdate(
					g_updateSchedule,
					audioDevice,
					true,
					now.QuadPart,
					PerformanceFrequency().QuadPart)) {
#ifdef GA_CLIENT_DEBUG
				g_commonSkippedThisFrame = true;
#endif
				return;
			}
		}

#ifdef GA_CLIENT_DEBUG
		LARGE_INTEGER start{};
		LARGE_INTEGER end{};
		g_lastGatherMilliseconds = 0.0;
		g_lastStopMilliseconds = 0.0;
		g_lastStartMilliseconds = 0.0;
		g_insideCommonUpdate = true;
		QueryPerformanceCounter(&start);
#endif
		m_original(audioDevice, edx, realtime);
#ifdef GA_CLIENT_DEBUG
		QueryPerformanceCounter(&end);
		g_insideCommonUpdate = false;

		const auto* device = static_cast<const AudioDeviceView*>(audioDevice);
		const std::int32_t sourceCapacity = std::max(0, device->sourceCapacity);
		const std::int32_t freeSources = std::clamp(
			device->freeSourceCount,
			0,
			sourceCapacity);
		g_lastCommonMilliseconds = ElapsedMilliseconds(start, end);
		g_lastAudioComponentCount = static_cast<std::uint32_t>(
			std::max(0, device->audioComponentCount));
		g_lastActiveSourceCount = static_cast<std::uint32_t>(
			sourceCapacity - freeSources);
		g_commonMeasuredThisFrame = true;
#endif
	}
};

#ifdef GA_CLIENT_DEBUG

class OpenAlAudioUpdateHook : public HookBase<
	void(__fastcall*)(void*, void*, int),
	// UALAudioDevice::Update wraps the common pass and submits OpenAL state.
	0x1095DAF0,
	OpenAlAudioUpdateHook> {
public:
	static void __fastcall Call(
		void* audioDevice,
		void* edx,
		int realtime) {
#ifdef GA_CLIENT_DEBUG
		g_commonMeasuredThisFrame = false;
		g_commonSkippedThisFrame = false;
		g_lastGatherMilliseconds = 0.0;
		g_lastStopMilliseconds = 0.0;
		g_lastStartMilliseconds = 0.0;
		LARGE_INTEGER start{};
		LARGE_INTEGER end{};
		QueryPerformanceCounter(&start);
#endif
		m_original(audioDevice, edx, realtime);
#ifdef GA_CLIENT_DEBUG
		QueryPerformanceCounter(&end);

		static ProfileWindow window{};
		static ULONGLONG windowStart = GetTickCount64();
		const double backendMilliseconds = ElapsedMilliseconds(start, end);
		const double commonMilliseconds = g_commonMeasuredThisFrame
			? g_lastCommonMilliseconds
			: 0.0;

		++window.frames;
		if (g_commonMeasuredThisFrame) ++window.executedUpdates;
		if (g_commonSkippedThisFrame) ++window.skippedUpdates;
		window.audioComponentTotal += g_lastAudioComponentCount;
		window.audioComponentMaximum = std::max(
			window.audioComponentMaximum,
			g_lastAudioComponentCount);
		window.activeSourceTotal += g_lastActiveSourceCount;
		window.activeSourceMaximum = std::max(
			window.activeSourceMaximum,
			g_lastActiveSourceCount);
		window.commonTotalMilliseconds += commonMilliseconds;
		window.commonMaximumMilliseconds = std::max(
			window.commonMaximumMilliseconds,
			commonMilliseconds);
		window.gatherTotalMilliseconds += g_lastGatherMilliseconds;
		window.gatherMaximumMilliseconds = std::max(
			window.gatherMaximumMilliseconds,
			g_lastGatherMilliseconds);
		window.stopTotalMilliseconds += g_lastStopMilliseconds;
		window.stopMaximumMilliseconds = std::max(
			window.stopMaximumMilliseconds,
			g_lastStopMilliseconds);
		window.startTotalMilliseconds += g_lastStartMilliseconds;
		window.startMaximumMilliseconds = std::max(
			window.startMaximumMilliseconds,
			g_lastStartMilliseconds);
		window.backendTotalMilliseconds += backendMilliseconds;
		window.backendMaximumMilliseconds = std::max(
			window.backendMaximumMilliseconds,
			backendMilliseconds);
		if (backendMilliseconds >= kSlowUpdateMilliseconds) {
			++window.slowFrames;
		}

		const ULONGLONG tickNow = GetTickCount64();
		const ULONGLONG elapsed = tickNow - windowStart;
		if (elapsed < 5000 || window.frames == 0) return;

		const double frameCount = static_cast<double>(window.frames);
		const double executedCount = static_cast<double>(
			std::max<std::uint32_t>(1, window.executedUpdates));
		const double backendExtraMilliseconds = std::max(
			0.0,
			(window.backendTotalMilliseconds -
			 window.commonTotalMilliseconds) / frameCount);
		const double otherRunMilliseconds = std::max(
			0.0,
			(window.commonTotalMilliseconds -
			 window.gatherTotalMilliseconds -
			 window.stopTotalMilliseconds -
			 window.startTotalMilliseconds) / executedCount);
		Logger::Log(
			"audioprofile",
			"[update] frames=%u executed=%u skipped=%u update-hz=%.1f "
			"voice-budget=%d components-avg=%.1f components-max=%u "
			"sources-avg=%.1f sources-max=%u "
			"common-frame-avg=%.3fms common-run-avg=%.3fms common-max=%.3fms "
			"gather-run-avg=%.3fms stop-run-avg=%.3fms "
			"start-run-avg=%.3fms other-run-avg=%.3fms "
			"openal-frame-avg=%.3fms openal-max=%.3fms "
			"openal-extra-avg=%.3fms slow-frames=%u\n",
			window.frames,
			window.executedUpdates,
			window.skippedUpdates,
			static_cast<double>(window.executedUpdates) * 1000.0 /
				static_cast<double>(elapsed),
			g_effectiveVoiceBudget,
			static_cast<double>(window.audioComponentTotal) / frameCount,
			window.audioComponentMaximum,
			static_cast<double>(window.activeSourceTotal) / frameCount,
			window.activeSourceMaximum,
			window.commonTotalMilliseconds / frameCount,
			window.commonTotalMilliseconds / executedCount,
			window.commonMaximumMilliseconds,
			window.gatherTotalMilliseconds / executedCount,
			window.stopTotalMilliseconds / executedCount,
			window.startTotalMilliseconds / executedCount,
			otherRunMilliseconds,
			window.backendTotalMilliseconds / frameCount,
			window.backendMaximumMilliseconds,
			backendExtraMilliseconds,
			window.slowFrames);

		window = {};
		windowStart = tickNow;
#endif
	}
};

#endif

}  // namespace

LONG ClientAudioUpdatePerformancePatch::Install() {
	LONG result = GatherWaveInstancesHook::Install();
#ifdef GA_CLIENT_DEBUG
	if (result == NO_ERROR) result = StopSourcesHook::Install();
	if (result == NO_ERROR) result = StartSourcesHook::Install();
#endif
	if (result == NO_ERROR) result = CommonAudioUpdateHook::Install();
#ifdef GA_CLIENT_DEBUG
	if (result == NO_ERROR) result = OpenAlAudioUpdateHook::Install();
#endif
	return result;
}
