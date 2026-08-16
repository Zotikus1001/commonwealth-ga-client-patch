#pragma once

#include <cstdint>

namespace ClientJetpackAimAlignment {

struct Rotator {
	std::int32_t Pitch;
	std::int32_t Yaw;
	std::int32_t Roll;
};

inline std::int32_t AddRotatorComponents(
	std::int32_t lhs, std::int32_t rhs) {
	const std::uint32_t wrapped =
		(static_cast<std::uint32_t>(lhs) +
		 static_cast<std::uint32_t>(rhs)) & 0xFFFFu;
	return wrapped > 0x7FFFu
		? static_cast<std::int32_t>(wrapped) - 0x10000
		: static_cast<std::int32_t>(wrapped);
}

inline Rotator CorrectedViewRotation(
	const Rotator& controllerRotation,
	const Rotator& shakeRotation) {
	// Roll does not affect forward aim. Excluding it keeps stale controller or
	// shake roll from rotating the rendered view while pitch/yaw remain aligned.
	return {
		AddRotatorComponents(controllerRotation.Pitch, shakeRotation.Pitch),
		AddRotatorComponents(controllerRotation.Yaw, shakeRotation.Yaw),
		0,
	};
}

inline bool IsInversionRisk(std::int32_t roll) {
	const std::int32_t normalized = AddRotatorComponents(roll, 0);
	return normalized <= -0x4000 || normalized >= 0x4000;
}

}  // namespace ClientJetpackAimAlignment
