#pragma once

#include <cmath>

namespace CombatTextScaleSetting {

inline constexpr int kMinimumPercent = 50;
inline constexpr int kDefaultPercent = 100;
inline constexpr int kMaximumPercent = 200;

inline int Clamp(int value) {
	if (value < kMinimumPercent) return kMinimumPercent;
	return value > kMaximumPercent ? kMaximumPercent : value;
}

inline int FromSlider(float value) {
	if (!std::isfinite(value)) return kDefaultPercent;
	if (value <= static_cast<float>(kMinimumPercent)) return kMinimumPercent;
	if (value >= static_cast<float>(kMaximumPercent)) return kMaximumPercent;
	return Clamp(static_cast<int>(std::floor(value + 0.5f)));
}

}  // namespace CombatTextScaleSetting
