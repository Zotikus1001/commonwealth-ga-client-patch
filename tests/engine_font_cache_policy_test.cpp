#include "src/ClientRuntime/EngineFontCachePolicy.hpp"

#include <cassert>
#include <cstdio>
#include <limits>

using namespace EngineFontCachePolicy;

int main() {
	char sourceA = 0;
	char sourceB = 0;
	char fontA = 0;
	char fontB = 0;
	void* objects[] = {nullptr, &fontA, &fontB};
	const Entry cached{&fontA, &sourceA, 1, false};

	assert(Validate(cached, &sourceA, false, objects, 3) == Status::Valid);
	assert(Validate(cached, &sourceB, false, objects, 3) ==
		Status::SourceChanged);
	assert(Validate(cached, &sourceA, true, objects, 3) ==
		Status::CandidateModeChanged);
	assert(Validate(
		cached, &sourceA, false, static_cast<void* const*>(nullptr), 3) ==
		Status::ObjectTableUnavailable);
	assert(Validate(Entry{&fontA, &sourceA, 3, false},
		&sourceA, false, objects, 3) == Status::IndexOutOfRange);
	assert(Validate(Entry{&fontA, &sourceA, -1, false},
		&sourceA, false, objects, 3) == Status::IndexOutOfRange);

	objects[1] = nullptr;
	assert(Validate(cached, &sourceA, false, objects, 3) ==
		Status::SlotChanged);
	objects[1] = &fontB;
	assert(Validate(cached, &sourceA, false, objects, 3) ==
		Status::SlotChanged);

	assert(Validate(Entry{}, &sourceA, false, objects, 3) == Status::Empty);

	objects[1] = &fontA;
	Entry entries[kEntryCapacity] = {};
	entries[0] = cached;
	entries[1] = {&fontB, &sourceB, 2, false};
	std::size_t alternatingMisses = 0;
	for (int iteration = 0; iteration < 10000; ++iteration) {
		const void* source = iteration % 2 == 0
			? static_cast<void*>(&sourceA)
			: static_cast<void*>(&sourceB);
		const std::size_t expected = iteration % 2 == 0 ? 0u : 1u;
		const std::size_t found = FindKey(
			entries, kEntryCapacity, source, false);
		if (found == kEntryCapacity) ++alternatingMisses;
		assert(found == expected);
		assert(Validate(entries[found], source, false, objects, 3) ==
			Status::Valid);
	}
	assert(alternatingMisses == 0);
	assert(FindKey(entries, kEntryCapacity, &sourceA, true) ==
		kEntryCapacity);
	assert(ChooseStorageIndex(entries, kEntryCapacity, 7) == 2);
	for (std::size_t index = 2; index < kEntryCapacity; ++index) {
		entries[index] = {&fontA, &sourceA, 1, true};
	}
	assert(ChooseStorageIndex(entries, kEntryCapacity, 10) == 2);

	std::atomic<std::uint32_t> nextScanTick{0};
	assert(TryReserveScan(nextScanTick, 100, 5000));
	assert(nextScanTick.load(std::memory_order_relaxed) == 5100);
	for (std::uint32_t now = 101; now < 5100; ++now) {
		assert(!TryReserveScan(nextScanTick, now, 5000));
	}
	assert(TryReserveScan(nextScanTick, 5100, 5000));
	assert(!TryReserveScan(nextScanTick, 5100, 5000));
	nextScanTick.store(0, std::memory_order_relaxed);
	assert(TryReserveScan(nextScanTick, 0x80000010u, 5000));

	constexpr std::uint32_t nearWrap =
		std::numeric_limits<std::uint32_t>::max() - 15u;
	nextScanTick.store(nearWrap, std::memory_order_relaxed);
	assert(TryReserveScan(nextScanTick, nearWrap, 32));
	assert(nextScanTick.load(std::memory_order_relaxed) == 16u);
	assert(!TryReserveScan(nextScanTick, 0u, 32));
	assert(TryReserveScan(nextScanTick, 16u, 32));
	nextScanTick.store(0, std::memory_order_relaxed);
	assert(TryReserveScan(nextScanTick,
		std::numeric_limits<std::uint32_t>::max() - 31u, 32));
	assert(nextScanTick.load(std::memory_order_relaxed) == 1u);

	std::puts("engine font cache policy tests passed");
	return 0;
}
