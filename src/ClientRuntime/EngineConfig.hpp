#pragma once

namespace EngineConfig {

enum class LoadIntResult {
	Missing,
	Invalid,
	Loaded,
};

LoadIntResult LoadInt(
	const wchar_t* section, const wchar_t* key, int* value);
bool SaveInt(const wchar_t* section, const wchar_t* key, int value);

}  // namespace EngineConfig
