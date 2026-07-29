#pragma once

// GLOBAL CLIENT PATCH: Morph Rebuild Performance
// Applies to LIVE and private servers in every client DLL build.
//
// Reattaching a morphed skeletal mesh recreates its render object. GA sends every configured morph target in
// that render-thread update, including zero-weight targets, and UE3 loops the full array while rebuilding the
// morph vertex buffer. Keep only contributing targets in the render copy before comparison/rebuild.
// Dynamic-data offsets and UpdateDynamicData_RenderThread at 0x111e0790 are Ghidra-verified for this client.

#include "src/pch.hpp"
#include "src/Utils/HookBase.hpp"

class ClientMorphRebuildPerformancePatch : public HookBase<
	void(__fastcall*)(void* /*meshObject=ECX*/, void* /*edx*/, void* /*incomingDynamicData*/),
	0x111e0790, ClientMorphRebuildPerformancePatch> {
public:
	static void __fastcall Call(void* meshObject, void* edx, void* incomingDynamicData);
	static inline void __fastcall CallOriginal(void* meshObject, void* edx, void* incomingDynamicData) {
		m_original(meshObject, edx, incomingDynamicData);
	}
};
