#pragma once

namespace EngineFontCachePolicy {

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
