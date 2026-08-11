#pragma once

#include <cstddef>

// Pure helpers for the spectator nameplates: value clamping, colour
// interpolation, and stat-line formatting.
//
// Deliberately free of Windows headers, engine ABI views and hard-coded
// addresses so it can be compiled and exercised on the host by
// tests/spectator_nameplate_format_test.cpp. The ini reader and the draw code
// hold the platform and engine dependencies; everything decidable without a
// running game lives here.
namespace SpectatorNameplateFormat {

// ── Settings ranges ────────────────────────────────────────────────────────
// Each ini value has exactly one place where its bounds are defined, so the
// tests pin the same limits the patch enforces.

inline constexpr int kScalePercentMin = 1;
inline constexpr int kBarWidthMin     = 4;
inline constexpr int kBarWidthMax     = 400;
inline constexpr int kBarHeightMin    = 1;
inline constexpr int kBarHeightMax    = 40;
inline constexpr int kFontMin         = 0;
inline constexpr int kFontMax         = 2;
inline constexpr int kHealthDisplayMin = 0;
inline constexpr int kHealthDisplayMax = 2;

inline int ClampInt(int value, int low, int high) {
	if (value < low)  return low;
	if (value > high) return high;
	return value;
}

// 0-255 ini colour channel.
inline int ClampChannel(int value) { return ClampInt(value, 0, 255); }

// Percent -> multiplier. A non-positive or absent percent falls back to the
// caller's default rather than collapsing the text to nothing.
inline float ScaleFromPercent(int percent, float fallback) {
	if (percent < kScalePercentMin) return fallback;
	return static_cast<float>(percent) / 100.0f;
}

// 0 disables the distance cull; anything negative is treated the same way
// rather than culling everything.
inline float MaxDistanceFromIni(int value) {
	return (value > 0) ? static_cast<float>(value) : 0.0f;
}

inline int ClampFont(int value) {
	return (value < kFontMin || value > kFontMax) ? 1 : value;
}

inline int ClampHealthDisplay(int value) {
	return (value < kHealthDisplayMin || value > kHealthDisplayMax) ? 1 : value;
}

inline int ClampBarWidth(int value)  { return ClampInt(value, kBarWidthMin, kBarWidthMax); }
inline int ClampBarHeight(int value) { return ClampInt(value, kBarHeightMin, kBarHeightMax); }

// ── Drawing maths ──────────────────────────────────────────────────────────

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 0-255 ini channel pair -> normalised colour component, interpolated by t.
inline float Lerp255(int lowByte, int highByte, float t) {
	const float lo = static_cast<float>(lowByte)  / 255.0f;
	const float hi = static_cast<float>(highByte) / 255.0f;
	return lo + (hi - lo) * t;
}

// Fraction of a bar to fill, guarding the max==0 case a pawn shows before it
// has finished initialising.
inline float BarFraction(float current, float maximum) {
	if (maximum <= 0.0f) return 0.0f;
	return Clamp01(current / maximum);
}

// ── Stat line ──────────────────────────────────────────────────────────────

// Minimal integer formatter — keeps swprintf and its CRT/locale baggage out of
// the render loop. Never writes past capacity-1 and always leaves room for the
// caller's terminator.
inline void AppendInt(wchar_t* buffer, int capacity, int& at, int value) {
	if (value < 0) value = 0;
	wchar_t digits[12];
	int n = 0;
	do { digits[n++] = static_cast<wchar_t>(L'0' + (value % 10)); value /= 10; }
	while (value && n < 12);
	while (n > 0 && at < capacity - 1) buffer[at++] = digits[--n];
}

// "hp/hpMax", optionally followed by "  pw/pwMax". Always NUL terminated.
inline void FormatStats(wchar_t* buffer, int capacity, int hp, int hpMax,
                        int pw, int pwMax, bool includePower) {
	if (capacity <= 0) return;
	int at = 0;
	AppendInt(buffer, capacity, at, hp);
	if (at < capacity - 1) buffer[at++] = L'/';
	AppendInt(buffer, capacity, at, hpMax);
	if (includePower) {
		if (at < capacity - 1) buffer[at++] = L' ';
		if (at < capacity - 1) buffer[at++] = L' ';
		AppendInt(buffer, capacity, at, pw);
		if (at < capacity - 1) buffer[at++] = L'/';
		AppendInt(buffer, capacity, at, pwMax);
	}
	buffer[at] = L'\0';
}

}  // namespace SpectatorNameplateFormat
