#pragma once

#include <atomic>
#include <cstdint>

// Wake-up handshake between UE3's render-command producer (game thread) and consumer (render thread).
//
// Retail behavior: every committed command signals the auto-reset data-written event, and the render thread
// blocks on that event whenever the ring buffer is empty. The consumer drains faster than the producer fills, so
// nearly every command pays a kernel wake of a sleeping thread. Here the consumer spins briefly before sleeping
// and advertises when it actually sleeps; the producer signals only then. Spurious signals are harmless (the event
// is auto-reset and the consumer re-checks the buffer); a missed signal is impossible because both sides use a
// store -> full fence -> load sequence (Dekker), so at least one observes the other's store.
namespace ClientRenderCommandSignal {

// How long the render thread polls for new commands before sleeping. Commands arrive microseconds apart while the
// game thread builds a frame, so this keeps the consumer awake for the busy part of the frame only.
constexpr std::int64_t kSpinMicroseconds = 500;

struct SignalState {
	std::atomic<std::int32_t> consumerSleeping{0};
};

enum class WaitOutcome : std::uint8_t {
	DataReady,
	Signalled,
	TimedOut,
};

inline void CpuRelax() {
#if defined(__i386__) || defined(__x86_64__)
	__builtin_ia32_pause();
#endif
}

// Producer side, called after the new write pointer has been stored. True when the consumer may be blocked and
// must be woken. The fence orders the (plain) write-pointer store before the flag load.
inline bool ShouldSignal(const SignalState& state) {
	std::atomic_thread_fence(std::memory_order_seq_cst);
	return state.consumerSleeping.load(std::memory_order_relaxed) != 0;
}

// Consumer side, replacing "if the buffer is empty, block on the event". `hasData` must re-read the ring buffer
// pointers on every call, `nowMicroseconds` is a monotonic clock, and `block` waits on the event and returns
// whether it was signalled (false on timeout).
template <typename HasData, typename NowMicroseconds, typename Block>
WaitOutcome WaitForData(
	SignalState& state,
	HasData hasData,
	NowMicroseconds nowMicroseconds,
	std::int64_t spinMicroseconds,
	Block block) {
	if (hasData()) return WaitOutcome::DataReady;

	const std::int64_t start = nowMicroseconds();
	while (nowMicroseconds() - start < spinMicroseconds) {
		for (int i = 0; i < 32; ++i) CpuRelax();
		if (hasData()) return WaitOutcome::DataReady;
	}

	state.consumerSleeping.store(1, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_seq_cst);
	if (hasData()) {
		state.consumerSleeping.store(0, std::memory_order_relaxed);
		return WaitOutcome::DataReady;
	}

	const bool signalled = block();
	state.consumerSleeping.store(0, std::memory_order_relaxed);
	return signalled ? WaitOutcome::Signalled : WaitOutcome::TimedOut;
}

}  // namespace ClientRenderCommandSignal
