#pragma once

#include <cmath>

namespace FovSetting {

inline constexpr int kDefaultFov = 90;
inline constexpr int kMaximumFov = 170;

inline int Clamp(int value) {
	if (value < kDefaultFov) return kDefaultFov;
	return value > kMaximumFov ? kMaximumFov : value;
}

inline int FromSlider(float value) {
	if (!std::isfinite(value)) return kDefaultFov;
	if (value <= static_cast<float>(kDefaultFov)) return kDefaultFov;
	if (value >= static_cast<float>(kMaximumFov)) return kMaximumFov;
	return Clamp(static_cast<int>(std::floor(value + 0.5f)));
}

}  // namespace FovSetting
