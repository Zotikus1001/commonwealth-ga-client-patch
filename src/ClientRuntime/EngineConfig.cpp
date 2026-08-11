#include "src/ClientRuntime/EngineConfig.hpp"

#include "src/pch.hpp"

#include <cerrno>
#include <cwchar>
#include <limits>

namespace {

constexpr std::uintptr_t kConfigSetIntAddress = 0x1131EFF0u;
constexpr std::uintptr_t kConfigFlushAddress = 0x1131C880u;
constexpr std::uintptr_t kGlobalConfigAddress = 0x134237DCu;
constexpr std::uintptr_t kEngineIniAddress = 0x1342C0A8u;

using ConfigSetIntFunction = void(__thiscall*)(
	void*, const wchar_t*, const wchar_t*, int, const wchar_t*);
using ConfigFlushFunction = void(__thiscall*)(void*, int, const wchar_t*);

std::wstring ResolveEngineIniPath() {
	std::vector<wchar_t> executablePath(MAX_PATH);
	DWORD pathLength = 0;
	for (;;) {
		pathLength = ::GetModuleFileNameW(
			nullptr,
			executablePath.data(),
			static_cast<DWORD>(executablePath.size()));
		if (pathLength == 0) return {};
		if (pathLength < executablePath.size()) break;
		if (executablePath.size() >= 32768) return {};
		executablePath.resize(executablePath.size() * 2);
	}

	std::wstring path(executablePath.data(), pathLength);
	const std::size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos) return {};
	path.resize(slash);
	path += L"\\..\\TgGame\\Config\\TgEngine.ini";
	const DWORD attributes = ::GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES ||
		(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		return {};
	}
	return path;
}

}  // namespace

EngineConfig::LoadIntResult EngineConfig::LoadInt(
	const wchar_t* section, const wchar_t* key, int* value) {
	if (!section || !key || !value) return LoadIntResult::Invalid;
	const std::wstring path = ResolveEngineIniPath();
	if (path.empty()) return LoadIntResult::Missing;

	wchar_t text[32]{};
	const DWORD length = ::GetPrivateProfileStringW(
		section,
		key,
		L"",
		text,
		static_cast<DWORD>(sizeof(text) / sizeof(text[0])),
		path.c_str());
	if (length == 0) return LoadIntResult::Missing;
	if (length >= (sizeof(text) / sizeof(text[0])) - 1) {
		return LoadIntResult::Invalid;
	}

	errno = 0;
	wchar_t* end = nullptr;
	const long parsed = std::wcstol(text, &end, 10);
	if (errno == ERANGE || end == text || *end != L'\0' ||
		parsed < std::numeric_limits<int>::min() ||
		parsed > std::numeric_limits<int>::max()) {
		return LoadIntResult::Invalid;
	}
	*value = static_cast<int>(parsed);
	return LoadIntResult::Loaded;
}

bool EngineConfig::SaveInt(
	const wchar_t* section, const wchar_t* key, int value) {
	if (!section || !key) return false;
	void* const configCache =
		*reinterpret_cast<void**>(kGlobalConfigAddress);
	const auto* const engineIni =
		reinterpret_cast<const wchar_t*>(kEngineIniAddress);
	if (!configCache || engineIni[0] == L'\0') return false;

	// Retail graphics Apply writes through this cache. Writing the file directly
	// is lost when the engine later flushes its older cached configuration.
	const auto setInt =
		reinterpret_cast<ConfigSetIntFunction>(kConfigSetIntAddress);
	const auto flush =
		reinterpret_cast<ConfigFlushFunction>(kConfigFlushAddress);
	setInt(configCache, section, key, value, engineIni);
	flush(configCache, 0, engineIni);
	return true;
}
