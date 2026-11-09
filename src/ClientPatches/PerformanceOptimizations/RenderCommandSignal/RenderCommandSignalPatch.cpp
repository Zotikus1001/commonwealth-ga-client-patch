#include "src/ClientPatches/PerformanceOptimizations/RenderCommandSignal/RenderCommandSignalPatch.hpp"

#include "src/ClientPatches/PerformanceOptimizations/RenderCommandSignal/RenderCommandSignalPolicy.hpp"
#include "src/ClientRuntime/PatchSetting.hpp"
#include "src/Utils/HookBase.hpp"

#ifdef GA_CLIENT_DEBUG
#include "src/Utils/Logger/Logger.hpp"

#include <atomic>
#endif

namespace {

using namespace ClientRenderCommandSignal;

// GRenderCommandBuffer: the FRingBuffer that the render thread loop drains (BeginRead 0x112C7750) and waits on.
constexpr std::uintptr_t kRenderCommandBufferAddress = 0x119A57C4u;

// FEventWin (vtable 0x118422FC): slot 2 Trigger (SetEvent), slot 5 Wait(ms) (WaitForSingleObject, RET 4).
struct EventView {
	void* const* vtable;
};
constexpr int kEventTriggerSlot = 2;
constexpr int kEventWaitSlot = 5;
using EventTriggerFn = void(__thiscall*)(EventView*);
using EventWaitFn = int(__thiscall*)(EventView*, std::uint32_t);

struct RingBufferView {
	void* dataStart;
	void* dataEnd;
	void* volatile writePointer;
	volatile std::int32_t isWriting;
	void* volatile readPointer;
	std::uint32_t alignment;
	EventView* dataWrittenEvent;
};
static_assert(offsetof(RingBufferView, writePointer) == 0x08, "Unexpected FRingBuffer::WritePointer offset");
static_assert(offsetof(RingBufferView, isWriting) == 0x0c, "Unexpected FRingBuffer::bIsWriting offset");
static_assert(offsetof(RingBufferView, readPointer) == 0x10, "Unexpected FRingBuffer::ReadPointer offset");
static_assert(offsetof(RingBufferView, dataWrittenEvent) == 0x18, "Unexpected FRingBuffer::DataWrittenEvent offset");

struct AllocationContextView {
	RingBufferView* ringBuffer;
	void* allocationStart;
	void* allocationEnd;
};
static_assert(offsetof(AllocationContextView, allocationStart) == 0x04, "Unexpected AllocationStart offset");
static_assert(offsetof(AllocationContextView, allocationEnd) == 0x08, "Unexpected AllocationEnd offset");

SignalState g_signal;

RingBufferView* RenderCommandBuffer() {
	return reinterpret_cast<RingBufferView*>(kRenderCommandBufferAddress);
}

void TriggerEvent(EventView* event) {
	reinterpret_cast<EventTriggerFn>(event->vtable[kEventTriggerSlot])(event);
}

bool WaitEvent(EventView* event, std::uint32_t milliseconds) {
	return reinterpret_cast<EventWaitFn>(event->vtable[kEventWaitSlot])(event, milliseconds) != 0;
}

std::int64_t NowMicroseconds() {
	static const std::int64_t frequency = [] {
		LARGE_INTEGER value{};
		QueryPerformanceFrequency(&value);
		return value.QuadPart > 0 ? value.QuadPart : 1;
	}();
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	return now.QuadPart / frequency * 1000000 + now.QuadPart % frequency * 1000000 / frequency;
}

#ifdef GA_CLIENT_DEBUG

// Commits and signals are counted on the game thread; waits are counted on the render thread.
std::uint64_t g_commits = 0;
std::uint64_t g_signals = 0;
std::atomic<std::uint32_t> g_spinHits{0};
std::atomic<std::uint32_t> g_sleeps{0};
std::atomic<std::uint32_t> g_timeouts{0};
// A timeout with commands already waiting means the producer published without signalling: a lost wake-up
// (or, rarely, a command published in the instant the wait expired). Must stay at or near zero.
std::atomic<std::uint32_t> g_timeoutsWithData{0};

void LogWindow() {
	static ULONGLONG windowStart = GetTickCount64();
	const ULONGLONG now = GetTickCount64();
	if (now - windowStart < 5000) return;
	Logger::Log(
		"rendersignal",
		"[window] seconds=%.1f commits=%llu signals=%llu consumer-spin-hits=%u consumer-sleeps=%u "
		"consumer-timeouts=%u timeouts-with-data=%u\n",
		static_cast<double>(now - windowStart) / 1000.0,
		static_cast<unsigned long long>(g_commits),
		static_cast<unsigned long long>(g_signals),
		g_spinHits.exchange(0, std::memory_order_relaxed),
		g_sleeps.exchange(0, std::memory_order_relaxed),
		g_timeouts.exchange(0, std::memory_order_relaxed),
		g_timeoutsWithData.exchange(0, std::memory_order_relaxed));
	g_commits = 0;
	g_signals = 0;
	windowStart = now;
}

#endif

class CommitHook : public HookBase<
	void(__fastcall*)(AllocationContextView*, void*),
	0x112C76B0u,
	CommitHook> {
public:
	static void __fastcall Call(AllocationContextView* context, void* edx) {
		RingBufferView* ring = context->ringBuffer;
		// Other ring buffers, empty contexts, and the one-time event creation keep the retail commit.
		if (ring != RenderCommandBuffer() || !context->allocationStart || !ring->dataWrittenEvent) {
			m_original(context, edx);
			return;
		}

		// Same publication as retail; only the unconditional DataWrittenEvent->Trigger() is made conditional.
		ring->writePointer = context->allocationEnd;
		ring->isWriting = 0;
		context->allocationStart = nullptr;
		const bool signal = ShouldSignal(g_signal);
		if (signal) TriggerEvent(ring->dataWrittenEvent);

#ifdef GA_CLIENT_DEBUG
		++g_commits;
		if (signal) ++g_signals;
		LogWindow();
#endif
	}
};

class WaitForReadHook : public HookBase<
	int(__fastcall*)(RingBufferView*, void*, std::uint32_t),
	0x112C7460u,
	WaitForReadHook> {
public:
	static int __fastcall Call(RingBufferView* ring, void* edx, std::uint32_t milliseconds) {
		if (ring != RenderCommandBuffer() || !ring->dataWrittenEvent) {
			return m_original(ring, edx, milliseconds);
		}

		const WaitOutcome outcome = WaitForData(
			g_signal,
			[ring] { return ring->readPointer != ring->writePointer; },
			NowMicroseconds,
			kSpinMicroseconds,
			[ring, milliseconds] { return WaitEvent(ring->dataWrittenEvent, milliseconds); });

#ifdef GA_CLIENT_DEBUG
		if (outcome == WaitOutcome::DataReady) g_spinHits.fetch_add(1, std::memory_order_relaxed);
		else g_sleeps.fetch_add(1, std::memory_order_relaxed);
		if (outcome == WaitOutcome::TimedOut) {
			g_timeouts.fetch_add(1, std::memory_order_relaxed);
			if (ring->readPointer != ring->writePointer) {
				g_timeoutsWithData.fetch_add(1, std::memory_order_relaxed);
			}
		}
#endif
		// The render loop ignores the result; mirror FEvent::Wait's "signalled" value where one exists.
		return outcome == WaitOutcome::Signalled ? 1 : 0;
	}
};

}  // namespace

LONG ClientRenderCommandSignalPatch::Install() {
	if (!PatchSetting::Enabled("RenderCommandSignal", true)) return NO_ERROR;
	LONG result = CommitHook::Install();
	if (result == NO_ERROR) result = WaitForReadHook::Install();
	return result;
}
