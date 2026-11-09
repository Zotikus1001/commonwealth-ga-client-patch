#include "src/ClientRuntime/PatchSetting.hpp"

#include "src/ClientRuntime/EngineConfig.hpp"
#include "src/ClientRuntime/PatchSettingPolicy.hpp"
#include "src/Utils/Logger/Logger.hpp"
#include "src/pch.hpp"

#include <cstring>
#include <string>

namespace {

constexpr wchar_t kConfigSection[] = L"Commonwealth.ClientPatch";

}  // namespace

bool PatchSetting::Enabled(const char* key, bool defaultEnabled) {
	if (!key) return defaultEnabled;
	// Keys are ASCII identifiers.
	const std::wstring wideKey(key, key + std::strlen(key));
	int value = 0;
	const EngineConfig::LoadIntResult result =
		EngineConfig::LoadInt(kConfigSection, wideKey.c_str(), &value);
	const PatchSettingPolicy::Decision decision = PatchSettingPolicy::Resolve(
		result != EngineConfig::LoadIntResult::Missing,
		result == EngineConfig::LoadIntResult::Loaded,
		value,
		defaultEnabled);
	Logger::Log(
		"clientpatch",
		"[setting] %s=%s (%s)\n",
		key,
		decision.enabled ? "on" : "off",
		PatchSettingPolicy::SourceName(decision.source));
	return decision.enabled;
}
