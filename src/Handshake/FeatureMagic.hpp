#pragma once

#include <cstdint>

namespace ClientFeatureMagic {

// The carrier is the stock reliable ClientCapBandwidth(int) RPC. Values whose
// high byte is 0x6D belong to this patch and are consumed before the retail
// bandwidth handler sees them. The remaining bits carry one stable feature ID
// and its exact server/client compatibility release:
//   [31:24] 0x6D | [23:16] feature ID | [15:0] feature release
//
// ID and release zero are reserved as invalid. MakeToken stays constexpr and
// does not validate them so this header can be mirrored verbatim in server
// code; client registration is the trust-boundary validation point.
using FeatureId = std::uint8_t;
using FeatureRelease = std::uint16_t;

inline constexpr std::uint32_t kMagic = 0x6D000000u;
inline constexpr std::uint32_t kMagicMask = 0xFF000000u;
inline constexpr std::uint32_t kFeatureIdMask = 0x00FF0000u;
inline constexpr std::uint32_t kFeatureReleaseMask = 0x0000FFFFu;

constexpr bool IsToken(std::int32_t token) {
	return (static_cast<std::uint32_t>(token) & kMagicMask) == kMagic;
}

// Encode/decode through unsigned values so bit operations remain defined even
// if a future carrier range sets the sign bit of the RPC's signed int.
constexpr std::int32_t MakeToken(
	FeatureId featureId,
	FeatureRelease featureRelease) {
	return static_cast<std::int32_t>(
		kMagic
		| (static_cast<std::uint32_t>(featureId) << 16u)
		| static_cast<std::uint32_t>(featureRelease));
}

constexpr FeatureId GetFeatureId(std::int32_t token) {
	return static_cast<FeatureId>(
		(static_cast<std::uint32_t>(token) & kFeatureIdMask) >> 16u);
}

constexpr FeatureRelease GetFeatureRelease(std::int32_t token) {
	return static_cast<FeatureRelease>(
		static_cast<std::uint32_t>(token) & kFeatureReleaseMask);
}

}  // namespace ClientFeatureMagic
