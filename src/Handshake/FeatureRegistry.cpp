#include "src/Handshake/FeatureRegistry.hpp"

#include <array>
#include <cstring>

namespace ClientFeatureHandshake {
namespace {

// A fixed table avoids heap allocation and mutable container topology inside
// ProcessEvent. Feature registration is startup-only, so removal is unnecessary.
constexpr std::size_t kMaximumFeatureCount = 32;
constexpr std::size_t kMaximumFeatureNameLength = 64;

struct Entry {
	bool used = false;
	bool mismatchNoticeIssued = false;
	FeatureState state = FeatureState::NotAdvertised;
	ClientFeatureMagic::FeatureRelease serverRelease = 0;
	Registration registration{};
};

std::array<Entry, kMaximumFeatureCount>& Entries() {
	// Function-local initialization keeps the standalone unit test simple and
	// guarantees initialization before the first registration.
	static std::array<Entry, kMaximumFeatureCount> entries{};
	return entries;
}

Entry* Find(ClientFeatureMagic::FeatureId featureId) {
	for (Entry& entry : Entries()) {
		if (entry.used && entry.registration.featureId == featureId) return &entry;
	}
	return nullptr;
}

void Deactivate(Entry& entry) {
	if (entry.state == FeatureState::Active
		&& entry.registration.activationHandler) {
		entry.registration.activationHandler(false);
	}
	entry.state = FeatureState::NotAdvertised;
	entry.serverRelease = 0;
}

bool IsValidFeatureName(const char* name) {
	// Names appear in local diagnostics and chat. Keep them short and printable
	// so a registration cannot inject control characters or unbounded text.
	if (!name) return false;
	for (std::size_t index = 0; index <= kMaximumFeatureNameLength; ++index) {
		const unsigned char character =
			static_cast<unsigned char>(name[index]);
		if (character == '\0') return index != 0;
		if (character < 0x20 || character > 0x7E) return false;
	}
	return false;
}

}  // namespace

bool Register(const Registration& registration) {
	// Zero values are reserved, and a stable diagnostic name is mandatory.
	if (registration.featureId == 0 || registration.featureRelease == 0
		|| !IsValidFeatureName(registration.name)) {
		return false;
	}
	// Exact repeated registration is harmless; an ID collision fails closed.
	if (Entry* existing = Find(registration.featureId)) {
		return existing->registration.featureRelease == registration.featureRelease
			&& std::strcmp(existing->registration.name, registration.name) == 0
			&& existing->registration.activationHandler
				== registration.activationHandler;
	}
	for (Entry& entry : Entries()) {
		if (entry.used) continue;
		entry.used = true;
		entry.registration = registration;
		return true;
	}
	return false;
}

bool HasRegistrations() {
	return RegistrationCount() != 0;
}

std::size_t RegistrationCount() {
	std::size_t count = 0;
	for (const Entry& entry : Entries()) {
		if (entry.used) ++count;
	}
	return count;
}

bool IsActive(ClientFeatureMagic::FeatureId featureId) {
	const Entry* entry = Find(featureId);
	return entry && entry->state == FeatureState::Active;
}

std::size_t CopyFeatureStatuses(
	FeatureStatus* destination,
	std::size_t capacity) {
	if (!destination || capacity == 0) return 0;

	std::size_t copied = 0;
	for (const Entry& entry : Entries()) {
		if (!entry.used) continue;
		if (copied >= capacity) break;
		destination[copied++] = {
			entry.registration.featureId,
			entry.registration.featureRelease,
			entry.serverRelease,
			entry.registration.name,
			entry.state,
		};
	}
	return copied;
}

TokenOutcome HandleServerToken(std::int32_t token) {
	TokenOutcome outcome{};
	if (!ClientFeatureMagic::IsToken(token)) return outcome;

	outcome.featureId = ClientFeatureMagic::GetFeatureId(token);
	outcome.serverRelease = ClientFeatureMagic::GetFeatureRelease(token);

	// Unknown and mismatched reserved tokens are intentionally consumed. Passing
	// them into the stock function would interpret protocol data as bandwidth.
	Entry* entry = Find(outcome.featureId);
	if (!entry) {
		outcome.result = TokenResult::UnknownFeature;
		return outcome;
	}
	outcome.clientRelease = entry->registration.featureRelease;
	outcome.featureName = entry->registration.name;
	if (outcome.clientRelease != outcome.serverRelease) {
		// A later conflicting advertisement must fail closed too. Without this,
		// a feature activated by an earlier match would remain usable after the
		// server advertised an incompatible release.
		Deactivate(*entry);
		entry->serverRelease = outcome.serverRelease;
		entry->state = FeatureState::ReleaseMismatch;
		outcome.result = TokenResult::FeatureReleaseMismatch;
		outcome.shouldNotifyUser = !entry->mismatchNoticeIssued;
		entry->mismatchNoticeIssued = true;
		return outcome;
	}
	if (entry->state == FeatureState::Active) {
		outcome.result = TokenResult::AlreadyActive;
		return outcome;
	}
	// Features without a callback can gate their hook bodies solely with
	// IsActive(). A callback is useful when activation owns extra runtime state.
	entry->serverRelease = outcome.serverRelease;
	if (entry->registration.activationHandler
		&& !entry->registration.activationHandler(true)) {
		entry->state = FeatureState::ActivationFailed;
		outcome.result = TokenResult::ActivationFailed;
		return outcome;
	}
	entry->state = FeatureState::Active;
	outcome.result = TokenResult::Activated;
	return outcome;
}

void Reset() {
	for (Entry& entry : Entries()) {
		if (!entry.used) continue;
		Deactivate(entry);
		entry.mismatchNoticeIssued = false;
	}
}

}  // namespace ClientFeatureHandshake
