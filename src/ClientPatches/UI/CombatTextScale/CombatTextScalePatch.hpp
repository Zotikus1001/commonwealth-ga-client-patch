#pragma once

#include "src/pch.hpp"
#include "src/Utils/HookBase.hpp"

class ClientCombatTextScalePatch;

using ClientCombatTextScalePatchBase = HookBase<
	void(__fastcall*)(void* /*canvas=ECX*/, void* /*edx*/, const void* /*text*/),
	0x10ed6720u, ClientCombatTextScalePatch>;

class ClientCombatTextScalePatch : public ClientCombatTextScalePatchBase {
public:
	static void Initialize();
	static LONG Install();
	static int ScalePercent();
	static bool ApplyScalePercent(int percent);
	static void __fastcall Call(void* canvas, void* edx, const void* text);

private:
	static inline void __fastcall CallOriginal(
		void* canvas, void* edx, const void* text) {
		m_original(canvas, edx, text);
	}
};
