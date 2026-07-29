#include <windows.h>
#include <dinput.h>

namespace {

// GlobalAgenda loads `dinput8.dll` from its executable directory before the
// system copy, which gives this patch its injection point. Every required
// export is forwarded to the absolute System32 DLL to preserve retail input
// behavior and avoid recursively loading this proxy.
HMODULE GetSystemDInput8() {
	static HMODULE module = []() -> HMODULE {
		char systemDir[MAX_PATH] = {};
		const UINT len = GetSystemDirectoryA(systemDir, MAX_PATH);
		if (len == 0 || len >= MAX_PATH) return nullptr;

		constexpr char kFileName[] = "\\dinput8.dll";
		if (len + sizeof(kFileName) > MAX_PATH) return nullptr;
		memcpy(systemDir + len, kFileName, sizeof(kFileName));

		return LoadLibraryA(systemDir);
	}();
	return module;
}

// Missing system exports fail with the closest API-appropriate error below
// instead of dereferencing a null function pointer during process startup.
FARPROC ResolveSystemExport(const char* name) {
	HMODULE module = GetSystemDInput8();
	return module ? GetProcAddress(module, name) : nullptr;
}

} // namespace

// The names and ordinals of these six forwarders are fixed in data/dinput8.def
// and match the 32-bit retail dinput8 surface used by GlobalAgenda.exe.
extern "C" HRESULT WINAPI DirectInput8Create(
	HINSTANCE hinst,
	DWORD dwVersion,
	REFIID riidltf,
	LPVOID* ppvOut,
	LPUNKNOWN punkOuter) {
	using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
	Fn fn = reinterpret_cast<Fn>(ResolveSystemExport("DirectInput8Create"));
	return fn ? fn(hinst, dwVersion, riidltf, ppvOut, punkOuter) : E_FAIL;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
	using Fn = HRESULT (WINAPI*)();
	Fn fn = reinterpret_cast<Fn>(ResolveSystemExport("DllCanUnloadNow"));
	return fn ? fn() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
	using Fn = HRESULT (WINAPI*)(REFCLSID, REFIID, LPVOID*);
	Fn fn = reinterpret_cast<Fn>(ResolveSystemExport("DllGetClassObject"));
	return fn ? fn(rclsid, riid, ppv) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
	using Fn = HRESULT (WINAPI*)();
	Fn fn = reinterpret_cast<Fn>(ResolveSystemExport("DllRegisterServer"));
	return fn ? fn() : E_FAIL;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
	using Fn = HRESULT (WINAPI*)();
	Fn fn = reinterpret_cast<Fn>(ResolveSystemExport("DllUnregisterServer"));
	return fn ? fn() : E_FAIL;
}

extern "C" const DIDATAFORMAT* WINAPI GetdfDIJoystick() {
	using Fn = const DIDATAFORMAT* (WINAPI*)();
	Fn fn = reinterpret_cast<Fn>(ResolveSystemExport("GetdfDIJoystick"));
	return fn ? fn() : nullptr;
}
