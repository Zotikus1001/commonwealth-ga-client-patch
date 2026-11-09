#include "src/ClientPatches/PerformanceOptimizations/RenderCommandSignal/RenderCommandSignalPolicy.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace ClientRenderCommandSignal;

namespace {

std::int64_t SteadyMicroseconds() {
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Auto-reset event with the same semantics as the engine's DataWrittenEvent.
class AutoResetEvent {
public:
	void Set() {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_signalled = true;
		}
		m_condition.notify_one();
	}

	bool Wait(std::chrono::milliseconds timeout) {
		std::unique_lock<std::mutex> lock(m_mutex);
		if (!m_condition.wait_for(lock, timeout, [this] { return m_signalled; })) return false;
		m_signalled = false;
		return true;
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_condition;
	bool m_signalled = false;
};

void TestSingleThreadedPaths() {
	SignalState state{};
	int blocks = 0;

	// Data already present: no spin, no sleep.
	assert(WaitForData(state, [] { return true; }, SteadyMicroseconds, 1000000, [&] { ++blocks; return true; })
		== WaitOutcome::DataReady);
	assert(blocks == 0);
	assert(state.consumerSleeping.load() == 0);

	// Data arrives while spinning: no sleep.
	int polls = 0;
	assert(WaitForData(state, [&] { return ++polls >= 3; }, SteadyMicroseconds, 1000000, [&] { ++blocks; return true; })
		== WaitOutcome::DataReady);
	assert(blocks == 0);
	assert(state.consumerSleeping.load() == 0);

	// Data arrives between advertising sleep and blocking: the re-check catches it.
	assert(WaitForData(
		state,
		[&] { return state.consumerSleeping.load() != 0; },
		SteadyMicroseconds,
		0,
		[&] { ++blocks; return true; }) == WaitOutcome::DataReady);
	assert(blocks == 0);
	assert(state.consumerSleeping.load() == 0);

	// No data: the consumer sleeps with the flag raised, and the producer would signal.
	bool sawSleeping = false;
	bool producerWouldSignal = false;
	assert(WaitForData(state, [] { return false; }, SteadyMicroseconds, 0, [&] {
		++blocks;
		sawSleeping = state.consumerSleeping.load() != 0;
		producerWouldSignal = ShouldSignal(state);
		return false;
	}) == WaitOutcome::TimedOut);
	assert(blocks == 1);
	assert(sawSleeping);
	assert(producerWouldSignal);
	assert(state.consumerSleeping.load() == 0);
	assert(!ShouldSignal(state));

	assert(WaitForData(state, [] { return false; }, SteadyMicroseconds, 0, [] { return true; })
		== WaitOutcome::Signalled);
}

// Producer/consumer stress with a long event timeout: a lost wake-up would stall the consumer until the timeout,
// which the test reports as a failure. Spin 0 forces the sleep path as often as possible.
void TestNoLostWakeups(std::int64_t spinMicroseconds) {
	constexpr std::uint64_t kCommands = 200000;
	SignalState state{};
	AutoResetEvent event;
	std::atomic<std::uint64_t> writePointer{0};
	std::atomic<std::uint64_t> signals{0};
	std::uint64_t readPointer = 0;
	std::uint32_t timeouts = 0;

	std::thread producer([&] {
		for (std::uint64_t i = 1; i <= kCommands; ++i) {
			writePointer.store(i, std::memory_order_relaxed);
			if (ShouldSignal(state)) {
				signals.fetch_add(1, std::memory_order_relaxed);
				event.Set();
			}
			// Idle gaps so the consumer regularly drains the buffer and goes to sleep.
			if (i % 64 == 0) std::this_thread::yield();
		}
	});

	while (readPointer < kCommands) {
		if (readPointer != writePointer.load(std::memory_order_relaxed)) {
			++readPointer;
			continue;
		}
		const WaitOutcome outcome = WaitForData(
			state,
			[&] { return readPointer != writePointer.load(std::memory_order_relaxed); },
			SteadyMicroseconds,
			spinMicroseconds,
			[&] { return event.Wait(std::chrono::milliseconds(2000)); });
		if (outcome == WaitOutcome::TimedOut) ++timeouts;
	}
	producer.join();

	assert(timeouts == 0);
	assert(signals.load() <= kCommands);
	std::printf("  spin=%lldus commands=%llu signals=%llu timeouts=%u\n",
		static_cast<long long>(spinMicroseconds),
		static_cast<unsigned long long>(kCommands),
		static_cast<unsigned long long>(signals.load()),
		timeouts);
}

}  // namespace

int main() {
	TestSingleThreadedPaths();
	TestNoLostWakeups(0);
	TestNoLostWakeups(kSpinMicroseconds);
	std::puts("render command signal tests passed");
	return 0;
}
