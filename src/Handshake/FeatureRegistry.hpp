#pragma once

#include "src/Handshake/FeatureMagic.hpp"

#include <cstddef>

namespace ClientFeatureHandshake {

// Called with true after an exact server token arrives and false when travel
// invalidates the current instance's advertisements. Returning false from the
// activation call keeps the feature inactive. Deactivation is best-effort.
using ActivationHandler = bool (*)(bool active);

// Registrations are process-lifetime metadata installed before hooks attach.
// `name` must point to static storage; it is used for duplicate validation and
// developer diagnostics, never sent over the wire.
struct Registration {
	ClientFeatureMagic::FeatureId featureId = 0;
	ClientFeatureMagic::FeatureRelease featureRelease = 0;
	const char* name = nullptr;
	ActivationHandler activationHandler = nullptr;
};

// Current-instance state used by DEBUG diagnostics and available to gated
// feature code without exposing the registry's internal storage.
enum class FeatureState {
	NotAdvertised,
	Active,
	ReleaseMismatch,
	ActivationFailed,
};

struct FeatureStatus {
	ClientFeatureMagic::FeatureId featureId = 0;
	ClientFeatureMagic::FeatureRelease clientRelease = 0;
	ClientFeatureMagic::FeatureRelease serverRelease = 0;
	const char* name = nullptr;
	FeatureState state = FeatureState::NotAdvertised;
};

enum class TokenResult {
	// NotMagic is the only result that permits the retail RPC to continue.
	// Every other result identifies a reserved 0x6D value that must be consumed.
	NotMagic,
	UnknownFeature,
	FeatureReleaseMismatch,
	AlreadyActive,
	Activated,
	ActivationFailed,
};

// HandleServerToken returns enough registration metadata for the ProcessEvent
// adapter to log and present a useful release-mismatch notice. Pointers refer
// to the process-lifetime registration and must not be freed by the caller.
struct TokenOutcome {
	TokenResult result = TokenResult::NotMagic;
	ClientFeatureMagic::FeatureId featureId = 0;
	ClientFeatureMagic::FeatureRelease serverRelease = 0;
	ClientFeatureMagic::FeatureRelease clientRelease = 0;
	const char* featureName = nullptr;
	bool shouldNotifyUser = false;
};

bool Register(const Registration& registration);
bool HasRegistrations();
std::size_t RegistrationCount();

// Server-dependent patch bodies use this as their fail-closed runtime gate.
bool IsActive(ClientFeatureMagic::FeatureId featureId);

// Copies process-lifetime registration metadata plus current-instance state
// into caller-owned storage. The return value is the number copied.
std::size_t CopyFeatureStatuses(
	FeatureStatus* destination,
	std::size_t capacity);

// Handles exactly one server advertisement. Matching tokens are idempotent; a
// mismatch deactivates any earlier match and requests one local notice per
// feature until the next Reset(). There is no response, heartbeat, or periodic
// server work.
TokenOutcome HandleServerToken(std::int32_t token);

// Deactivate current-instance features while retaining their registrations so
// the next instance can advertise them again.
void Reset();

}  // namespace ClientFeatureHandshake
