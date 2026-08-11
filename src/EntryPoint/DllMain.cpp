// Patch-only DInput8 proxy for the reviewed retail Global Agenda client.

#include "src/pch.hpp"

#include "src/ClientPatches/MorphRebuildPerformance/MorphRebuildPerformancePatch.hpp"
#include "src/ClientPatches/ScopedWeaponVisibility/ScopedWeaponVisibilityPatch.hpp"
#include "src/ClientPatches/UI/CombatTextScale/CombatTextScalePatch.hpp"
#include "src/ClientPatches/UI/F2StatsScaling/F2StatsScalingPatch.hpp"
#include "src/ClientPatches/UI/FovSlider/FovSliderPatch.hpp"
#include "src/Handshake/FeatureHandshakePatch.hpp"
#include "src/Utils/ClientLogDirectory/ClientLogDirectory.hpp"
#include "src/Utils/ClientExecutableGuard/ClientExecutableGuard.hpp"
#include "src/Utils/CrashHandler/CrashHandler.hpp"
#include "src/Utils/Logger/Logger.hpp"
#include "src/Handshake/ClientFeatureRegistry.hpp"

namespace {

std::string ExecutableFolder() {
	// Use the process image, not this proxy DLL, as the anchor. This stays
	// correct if another loader redirects the DLL from a different directory.
	std::vector<char> modulePath(MAX_PATH);
	DWORD pathLength = 0;
	for (;;) {
		SetLastError(ERROR_SUCCESS);
		pathLength = GetModuleFileNameA(
			nullptr,
			modulePath.data(),
			static_cast<DWORD>(modulePath.size()));
		if (pathLength == 0) return {};
		if (pathLength < modulePath.size()) break;
		if (modulePath.size() >= 32768) return {};
		modulePath.resize(modulePath.size() * 2);
	}

	const std::string path(modulePath.data(), pathLength);
	const std::size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

bool InitializeRuntimeDiagnostics() {
	// Never fall back to a relative path: the process working directory is not a
	// reliable proxy for the executable directory and could pollute another
	// folder if image-path discovery ever fails.
	const std::string executableFolder = ExecutableFolder();
	if (executableFolder.empty()) return false;

	const std::string logsRoot = executableFolder + "\\logs";
	if (!ClientLogDirectory::Initialize(logsRoot)) return false;

	// Release retains only lifecycle/patch status and crash breadcrumbs. Debug
	// additionally records the detailed morph/visibility profiling channel.
	Logger::EnableChannel("clientpatch");
	Logger::EnableCrashChannel("clientpatch");
#ifdef GA_CLIENT_DEBUG
	Logger::EnableChannel("morphprofile");
	Logger::EnableCrashChannel("morphprofile");
#endif

	// Crash capture snapshots the same atomically published directory used by
	// normal logs, including each later DEBUG instance rotation.
	CrashHandler::Install();
	return true;
}

DWORD WINAPI InstallHooks(LPVOID) {
	// Run allocations, filesystem setup, executable hashing, and Detours work
	// outside DllMain's loader lock.
	if (!InitializeRuntimeDiagnostics()) {
		OutputDebugStringA(
			"Client patch disabled: runtime diagnostics initialization failed.\n");
		return ERROR_PATH_NOT_FOUND;
	}

	// Hard-coded hook addresses are safe only for the exact reviewed executable.
	// Unsupported binaries continue running with every patch disabled.
	const auto status = ClientExecutableGuard::Status();
	const auto& fingerprint = ClientExecutableGuard::ExecutableFingerprint();
	if (status != ClientExecutableGuard::SupportStatus::Supported) {
		Logger::Log(
			"clientpatch",
			"[unsupported] hooks skipped: status=%s timestamp=0x%08lx "
			"image-size=0x%08lx text-sha256-prefix=0x%016llx\n",
			ClientExecutableGuard::StatusName(status),
			static_cast<unsigned long>(fingerprint.peTimestamp),
			static_cast<unsigned long>(fingerprint.imageSize),
			static_cast<unsigned long long>(fingerprint.textHashPrefix));
		return 0;
	}
	ClientCombatTextScalePatch::Initialize();
	ClientFovSliderPatch::Initialize();
	if (!RegisterClientFeatures()) {
		Logger::Log(
			"clientpatch",
			"[fatal] feature registration failed; hooks not applied\n");
		return ERROR_INVALID_DATA;
	}

	// Attach all hooks atomically. A single attach failure aborts the transaction
	// so the process never runs with an unintended partial patch set.
	LONG result = ::DetourTransactionBegin();
	if (result != NO_ERROR) {
		Logger::Log(
			"clientpatch",
			"[fatal] DetourTransactionBegin failed (%ld); hooks not applied\n",
			static_cast<long>(result));
		return static_cast<DWORD>(result);
	}

	result = ::DetourUpdateThread(::GetCurrentThread());
	if (result == NO_ERROR) result = ClientMorphRebuildPerformancePatch::Install();
	if (result == NO_ERROR) result = ClientScopedWeaponVisibilityPatch::Install();
	if (result == NO_ERROR) result = ClientCombatTextScalePatch::Install();
	if (result == NO_ERROR) result = ClientF2StatsScalingPatch::Install();
	if (result == NO_ERROR) result = ClientFovSliderPatch::Install();
	if (result == NO_ERROR) result = FeatureHandshakePatch::InstallIfNeeded();
	if (result != NO_ERROR) {
		::DetourTransactionAbort();
		Logger::Log(
			"clientpatch",
			"[fatal] hook attach failed (%ld); transaction aborted\n",
			static_cast<long>(result));
		return static_cast<DWORD>(result);
	}

	result = ::DetourTransactionCommit();
	if (result != NO_ERROR) {
		Logger::Log(
			"clientpatch",
			"[fatal] DetourTransactionCommit failed (%ld); hooks not applied\n",
			static_cast<long>(result));
		return static_cast<DWORD>(result);
	}

	Logger::Log(
		"clientpatch",
		"[ready] client patches installed: morph rebuild, scoped weapon visibility, "
		"FOV slider, combat text scaling, F2 stats scaling, "
		"friendly name normalization\n");
	return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
	if (reason != DLL_PROCESS_ATTACH) return TRUE;
	DisableThreadLibraryCalls(module);

	// Never wait for this worker from DllMain: the loader lock must be released
	// before the worker touches other DLLs or initializes the C++ runtime state.
	HANDLE thread = CreateThread(nullptr, 0, InstallHooks, nullptr, 0, nullptr);
	if (thread) CloseHandle(thread);
	return TRUE;
}
