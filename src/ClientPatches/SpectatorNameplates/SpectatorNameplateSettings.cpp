#include "src/ClientPatches/SpectatorNameplates/SpectatorNameplateSettings.hpp"

#include "src/ClientPatches/SpectatorNameplates/SpectatorNameplateFormat.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace {

constexpr DWORD kReloadIntervalMs = 1000;

// Anchor on the process image, not this DLL — matches how the rest of the
// patch resolves paths, and stays correct if a loader redirects the DLL.
std::string IniPath() {
	std::vector<char> modulePath(MAX_PATH);
	DWORD length = 0;
	for (;;) {
		SetLastError(ERROR_SUCCESS);
		length = GetModuleFileNameA(
			nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
		if (length == 0) return {};
		if (length < modulePath.size()) break;
		if (modulePath.size() >= 32768) return {};
		modulePath.resize(modulePath.size() * 2);
	}
	const std::string path(modulePath.data(), length);
	const std::size_t slash = path.find_last_of("\\/");
	if (slash == std::string::npos) return {};
	return path.substr(0, slash) + "\\SpectatorNameplates.ini";
}

int ReadInt(const std::string& path, const char* key, int fallback) {
	return static_cast<int>(
		GetPrivateProfileIntA("Nameplates", key, fallback, path.c_str()));
}

}  // namespace

const SpectatorNameplateSettings& SpectatorNameplateSettings::Get() {
	static SpectatorNameplateSettings s_current;
	static std::string s_path;
	static DWORD s_lastLoad = 0;
	static bool s_initialised = false;

	const DWORD now = GetTickCount();
	if (s_initialised && (now - s_lastLoad) < kReloadIntervalMs) return s_current;
	s_lastLoad = now;

	if (!s_initialised) {
		s_path = IniPath();
		s_initialised = true;
	}
	if (s_path.empty()) return s_current;  // keep defaults

	// Percent rather than a float key: GetPrivateProfileInt avoids pulling in
	// string parsing and locale-dependent float conversion for one value.
	const int scalePercent = ReadInt(s_path, "ScalePercent", 160);
	s_current.scale = SpectatorNameplateFormat::ScaleFromPercent(scalePercent, 1.6f);

	s_current.heightOffset = static_cast<float>(ReadInt(s_path, "HeightOffset", 55));

	s_current.shadowAlpha = SpectatorNameplateFormat::ClampChannel(
		ReadInt(s_path, "ShadowAlpha", 200));

	s_current.maxDistance = SpectatorNameplateFormat::MaxDistanceFromIni(
		ReadInt(s_path, "MaxDistance", 0));

	s_current.font = SpectatorNameplateFormat::ClampFont(ReadInt(s_path, "Font", 1));

	s_current.healthDisplay = SpectatorNameplateFormat::ClampHealthDisplay(
		ReadInt(s_path, "HealthDisplay", 1));

	s_current.barWidth = SpectatorNameplateFormat::ClampBarWidth(
		ReadInt(s_path, "BarWidth", 60));

	s_current.barHeight = SpectatorNameplateFormat::ClampBarHeight(
		ReadInt(s_path, "BarHeight", 5));

	s_current.showPowerBar = ReadInt(s_path, "ShowPowerBar", 0) != 0;

	auto channel = [&](const char* key, int fallback) {
		return SpectatorNameplateFormat::ClampChannel(ReadInt(s_path, key, fallback));
	};
	s_current.healthFullR = channel("HealthFullR", 40);
	s_current.healthFullG = channel("HealthFullG", 230);
	s_current.healthFullB = channel("HealthFullB", 60);
	s_current.healthLowR  = channel("HealthLowR", 235);
	s_current.healthLowG  = channel("HealthLowG", 45);
	s_current.healthLowB  = channel("HealthLowB", 40);
	s_current.powerR      = channel("PowerR", 40);
	s_current.powerG      = channel("PowerG", 140);
	s_current.powerB      = channel("PowerB", 255);
	s_current.backdropAlpha = channel("BackdropAlpha", 165);

	return s_current;
}
