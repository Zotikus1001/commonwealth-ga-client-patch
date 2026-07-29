#pragma once

#include "src/Utils/HookBase.hpp"

// OPTIONAL SERVER-FEATURE PROCESS-EVENT HOOK
//
// Release builds attach this detour only when ClientFeatureRegistry contains a
// server-dependent feature. DEBUG builds also attach it to show a one-shot
// local patch/feature summary after each instance join. The hook consumes the
// reserved ClientCapBandwidth values and resets feature state at travel
// boundaries.
//
// UObject::ProcessEvent at 0x11347C20 and the calling convention are verified
// for the executable accepted by ClientExecutableGuard.
class FeatureHandshakePatch : public HookBase<
	void(__fastcall*)(UObject*, void*, UFunction*, void*, void*),
	0x11347C20,
	FeatureHandshakePatch> {
public:
	using Base = HookBase<
		void(__fastcall*)(UObject*, void*, UFunction*, void*, void*),
		0x11347C20,
		FeatureHandshakePatch>;

	static LONG InstallIfNeeded();
	static void __fastcall Call(
		UObject* object,
		void* edx,
		UFunction* function,
		void* params,
		void* result);
	static void __fastcall CallOriginal(
		UObject* object,
		void* edx,
		UFunction* function,
		void* params,
		void* result) {
		m_original(object, edx, function, params, result);
	}
};
