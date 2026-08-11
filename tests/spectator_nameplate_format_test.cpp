#include "src/ClientPatches/UI/SpectatorNameplates/SpectatorNameplateFormat.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <limits>

namespace {

using namespace SpectatorNameplateFormat;

bool NearlyEqual(float a, float b) { return std::fabs(a - b) < 0.0005f; }

// Format into a fixed buffer and compare against an expected wide string.
bool StatsAre(int capacity, int hp, int hpMax, int pw, int pwMax,
              bool includePower, const wchar_t* expected) {
	wchar_t buffer[128];
	for (int i = 0; i < 128; ++i) buffer[i] = L'#';
	FormatStats(buffer, capacity, hp, hpMax, pw, pwMax, includePower);
	if (std::wcscmp(buffer, expected) != 0) return false;
	// Never writes past the capacity it was handed.
	for (int i = capacity; i < 128; ++i) {
		if (buffer[i] != L'#') return false;
	}
	return true;
}

void TestSettingsClamps() {
	// Colour channels saturate at both ends and pass sane values through.
	assert(ClampChannel(-1) == 0);
	assert(ClampChannel(0) == 0);
	assert(ClampChannel(165) == 165);
	assert(ClampChannel(255) == 255);
	assert(ClampChannel(9999) == 255);

	// Scale: a missing or nonsensical percent falls back rather than
	// collapsing the nameplate to zero size.
	assert(NearlyEqual(ScaleFromPercent(160, 1.6f), 1.6f));
	assert(NearlyEqual(ScaleFromPercent(100, 1.6f), 1.0f));
	assert(NearlyEqual(ScaleFromPercent(1, 1.6f), 0.01f));
	assert(NearlyEqual(ScaleFromPercent(400, 1.6f), 4.0f));
	assert(NearlyEqual(ScaleFromPercent(401, 1.6f), 4.0f));
	assert(NearlyEqual(
		ScaleFromPercent(std::numeric_limits<int>::max(), 1.6f), 4.0f));
	assert(NearlyEqual(ScaleFromPercent(0, 1.6f), 1.6f));
	assert(NearlyEqual(ScaleFromPercent(-50, 1.6f), 1.6f));
	assert(NearlyEqual(MultiplyScale(1.6f, 0.5f), 0.8f));
	assert(NearlyEqual(MultiplyScale(1.6f, 1.5f), 2.4f));
	assert(NearlyEqual(MultiplyScale(4.0f, 2.0f), 4.0f));
	assert(NearlyEqual(MultiplyScale(1.6f,
		std::numeric_limits<float>::quiet_NaN()), 1.6f));

	assert(NearlyEqual(HeightOffsetFromIni(-20000), -10000.0f));
	assert(NearlyEqual(HeightOffsetFromIni(55), 55.0f));
	assert(NearlyEqual(HeightOffsetFromIni(20000), 10000.0f));

	// MaxDistance: 0 means "no cull", and a negative ini value must mean the
	// same thing rather than culling every plate.
	assert(NearlyEqual(MaxDistanceFromIni(0), 0.0f));
	assert(NearlyEqual(MaxDistanceFromIni(-1), 0.0f));
	assert(NearlyEqual(MaxDistanceFromIni(4000), 4000.0f));

	// Font and HealthDisplay are enums: out-of-range falls back to 1, which is
	// the shipped default in both cases.
	assert(ClampFont(0) == 0);
	assert(ClampFont(1) == 1);
	assert(ClampFont(2) == 2);
	assert(ClampFont(3) == 1);
	assert(ClampFont(-1) == 1);
	assert(ClampHealthDisplay(0) == 0);
	assert(ClampHealthDisplay(2) == 2);
	assert(ClampHealthDisplay(7) == 1);
	assert(ClampHealthDisplay(-3) == 1);

	// Bar geometry clamps to a drawable range.
	assert(ClampBarWidth(0) == kBarWidthMin);
	assert(ClampBarWidth(60) == 60);
	assert(ClampBarWidth(100000) == kBarWidthMax);
	assert(ClampBarHeight(0) == kBarHeightMin);
	assert(ClampBarHeight(5) == 5);
	assert(ClampBarHeight(100000) == kBarHeightMax);
	assert(NearlyEqual(ScaledBarHeight(5, 1.6f), 8.0f));
	assert(NearlyEqual(ScaledBarHeight(kBarHeightMax, 4.0f),
		static_cast<float>(kScaledBarHeightMax)));

	assert(SafeArrayCount(66, 66) == 66);
	assert(SafeArrayCount(300, 300) == kPriEntriesMax);
	assert(SafeArrayCount(4, 3) == 0);
	assert(SafeArrayCount(-1, 10) == 0);
	assert(IsValidNameCount(2, 2));
	assert(IsValidNameCount(kNameCodeUnitsMax, kNameCodeUnitsMax));
	assert(!IsValidNameCount(1, 1));
	assert(!IsValidNameCount(5, 4));
	assert(!IsValidNameCount(kNameCodeUnitsMax + 1, kNameCodeUnitsMax + 1));
}

void TestColourMaths() {
	assert(NearlyEqual(Clamp01(-0.5f), 0.0f));
	assert(NearlyEqual(Clamp01(0.25f), 0.25f));
	assert(NearlyEqual(Clamp01(1.5f), 1.0f));

	// t=0 gives the low colour, t=1 the full colour, normalised to 0..1.
	assert(NearlyEqual(Lerp255(0, 255, 0.0f), 0.0f));
	assert(NearlyEqual(Lerp255(0, 255, 1.0f), 1.0f));
	assert(NearlyEqual(Lerp255(0, 255, 0.5f), 0.5f));
	// Red-at-low to green-at-full: half health sits midway on each channel.
	assert(NearlyEqual(Lerp255(235, 40, 0.0f), 235.0f / 255.0f));
	assert(NearlyEqual(Lerp255(235, 40, 1.0f), 40.0f / 255.0f));

	// A pawn that has not finished initialising reports max 0; the bar must
	// read empty rather than dividing by zero.
	assert(NearlyEqual(BarFraction(100.0f, 0.0f), 0.0f));
	assert(NearlyEqual(BarFraction(50.0f, 100.0f), 0.5f));
	assert(NearlyEqual(BarFraction(0.0f, 100.0f), 0.0f));
	assert(NearlyEqual(BarFraction(100.0f, 100.0f), 1.0f));
	// Overheal / negative health must not push the bar outside the track.
	assert(NearlyEqual(BarFraction(150.0f, 100.0f), 1.0f));
	assert(NearlyEqual(BarFraction(-10.0f, 100.0f), 0.0f));
	assert(NearlyEqual(BarFraction(std::numeric_limits<float>::infinity(), 100.0f), 0.0f));
	assert(NearlyEqual(BarFraction(50.0f, std::numeric_limits<float>::quiet_NaN()), 0.0f));

	assert(RoundedStat(-1.0f) == 0);
	assert(RoundedStat(10.49f) == 10);
	assert(RoundedStat(10.5f) == 11);
	assert(RoundedStat(std::numeric_limits<float>::quiet_NaN()) == 0);
	assert(RoundedStat(std::numeric_limits<float>::infinity()) == 0);
}

void TestStatFormatting() {
	assert(StatsAre(64, 750, 1200, 0, 0, false, L"750/1200"));
	assert(StatsAre(64, 0, 1200, 0, 0, false, L"0/1200"));
	// Health is read straight off the pawn and goes negative on a killing
	// blow; the plate must not print a stray minus sign.
	assert(StatsAre(64, -25, 1200, 0, 0, false, L"0/1200"));

	// includePower appends the power pair after a two-space gap.
	assert(StatsAre(64, 750, 1200, 40, 100, true, L"750/1200  40/100"));
	assert(StatsAre(64, 750, 1200, 0, 0, true, L"750/1200  0/0"));

	// Truncation: a capacity too small to hold the whole line must still
	// produce a terminated string and stay inside the buffer.
	assert(StatsAre(5, 750, 1200, 0, 0, false, L"750/"));
	assert(StatsAre(1, 750, 1200, 0, 0, false, L""));

	// Capacity 0 is a no-op — nothing is written at all.
	wchar_t untouched[4] = { L'#', L'#', L'#', L'#' };
	FormatStats(untouched, 0, 750, 1200, 0, 0, false);
	assert(untouched[0] == L'#');

	// AppendInt advances the caller's cursor and handles multi-digit values.
	wchar_t buffer[16];
	int at = 0;
	AppendInt(buffer, 16, at, 7);
	AppendInt(buffer, 16, at, 1234567);
	buffer[at] = L'\0';
	assert(std::wcscmp(buffer, L"71234567") == 0);
	assert(at == 8);
}

}  // namespace

int main() {
	TestSettingsClamps();
	TestColourMaths();
	TestStatFormatting();

	std::puts("spectator nameplate format tests passed");
	return 0;
}
