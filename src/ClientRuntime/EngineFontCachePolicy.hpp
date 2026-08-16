#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace EngineFontCachePolicy {

inline constexpr std::size_t kEntryCapacity = 8;

enum class Status {
	Empty,
	Valid,
	SourceChanged,
	CandidateModeChanged,
	ObjectTableUnavailable,
	IndexOutOfRange,
	SlotChanged,
	IdentityChanged,
};

struct Entry {
	void* font = nullptr;
	void* sourceFont = nullptr;
	int objectIndex = -1;
	bool bold = false;
};

inline bool KeyMatches(
	const Entry& entry, const void* sourceFont, bool bold) {
	return entry.sourceFont == sourceFont && entry.bold == bold;
}

inline std::size_t FindKey(
	const Entry* entries, std::size_t count,
	const void* sourceFont, bool bold) {
	if (!entries) return count;
	for (std::size_t index = 0; index < count; ++index) {
		if (KeyMatches(entries[index], sourceFont, bold)) return index;
	}
	return count;
}

inline std::size_t ChooseStorageIndex(
	const Entry* entries, std::size_t count, std::uint32_t victimSequence) {
	if (!entries || count == 0) return count;
	for (std::size_t index = 0; index < count; ++index) {
		if (!entries[index].font) return index;
	}
	return victimSequence % count;
}

inline bool TickReached(std::uint32_t now, std::uint32_t target) {
	return now - target < 0x80000000u;
}

inline bool TryReserveScan(
	std::atomic<std::uint32_t>& nextScanTick,
	std::uint32_t now, std::uint32_t retryMilliseconds) {
	std::uint32_t next = nextScanTick.load(std::memory_order_relaxed);
	if (next != 0 && !TickReached(now, next)) return false;
	std::uint32_t deadline = now + retryMilliseconds;
	if (deadline == 0) deadline = 1;
	return nextScanTick.compare_exchange_strong(
		next, deadline,
		std::memory_order_acq_rel, std::memory_order_relaxed);
}

template <typename TObject>
inline Status Validate(
	const Entry& entry, const void* sourceFont, bool bold,
	TObject* const* objects, int objectCount) {
	if (!entry.font) return Status::Empty;
	if (entry.sourceFont != sourceFont) return Status::SourceChanged;
	if (entry.bold != bold) return Status::CandidateModeChanged;
	if (!objects || objectCount < 0) return Status::ObjectTableUnavailable;
	if (entry.objectIndex < 0 || entry.objectIndex >= objectCount) {
		return Status::IndexOutOfRange;
	}
	return static_cast<const void*>(objects[entry.objectIndex]) == entry.font
		? Status::Valid
		: Status::SlotChanged;
}

}  // namespace EngineFontCachePolicy
