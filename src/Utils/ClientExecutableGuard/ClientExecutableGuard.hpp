#pragma once

#include <cstdint>

namespace ClientExecutableGuard {

// The guard distinguishes an unreadable/invalid executable from a valid but
// unsupported build so the patch log gives developers actionable evidence.
enum class SupportStatus : std::uint8_t {
	Unchecked = 0,
	Supported,
	ExecutableUnavailable,
	InvalidExecutable,
	UnsupportedExecutable,
};

struct Fingerprint {
	// The short fields are diagnostic only. Support requires the complete
	// .text SHA-256 stored privately by the implementation to match as well.
	std::uint32_t peTimestamp = 0;
	std::uint32_t imageSize = 0;
	std::uint64_t textHashPrefix = 0;
};

// All accessors share one thread-safe, process-lifetime inspection.
SupportStatus Status();
const Fingerprint& ExecutableFingerprint();
const char* StatusName(SupportStatus status);
bool IsSupported();

}  // namespace ClientExecutableGuard
