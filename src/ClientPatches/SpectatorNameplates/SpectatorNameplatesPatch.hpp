#pragma once

#include "src/Utils/HookBase.hpp"

// SPECTATOR OVERHEAD OVERLAYS
//
// TgHUD_Game::DrawActorOverlays is the native per-pawn iterator that dispatches
// PostRenderFor, i.e. everything drawn above a pawn (tags, names, health bars).
// It refuses to run for a spectator, so a spectating client sees a bare world.
//
// The native's gate chain, from the decompile at 0x113AA6F0 (the HUD arrives as
// int*, so param_1[N] is byte offset N*4):
//
//   if (param_1[0x36] != 0)                    // +0x0D8 WorldInfo
//   if ((*(byte*)(param_1 + 0x120) & 2) == 0)  // +0x480 m_bDisablePostRender false
//   iVar7 = vtable[0x498]();
//   if (iVar7 == 0                             //       must return 0
//       && param_1[0x119] != 0                 // +0x464 m_PlayerOwner
//       && param_1[0x10b] != 0)                // +0x42C Canvas
//     if ((param_1[0x11a] != 0)                // +0x468 m_PawnOwner      <-- BLOCKER
//         && ((param_1[0x19e] & 1U) != 0)      // +0x678 m_bDrawPawnHUD   <-- BLOCKER
//         && ((param_1[0x19e] & 2U) == 0)      // +0x678 m_bBlockPawnHUD false
//         && worldInfo[0x32C] != 0)
//
// A spectator never spawns a pawn, so m_PawnOwner stays null and m_bDrawPawnHUD
// stays false. Every other condition is already satisfied — the
// PlayerController, Canvas and WorldInfo all exist while spectating.
//
// Both blockers are client-only HUD fields that never touch the wire, so no
// server change can reach them; this has to be a client patch.
//
// WHAT THIS DOES
// For spectator HUDs only: lends the spectated pawn to m_PawnOwner, forces
// m_bDrawPawnHUD on and m_bBlockPawnHUD off, runs the original, then restores
// all three. Non-spectators are passed through untouched and the borrowed state
// never outlives the call.
//
// LOCAL-ONLY FIX — deliberately not registered in ClientFeatureRegistry. It
// self-detects spectator status from the local PRI and needs no server
// negotiation, so registering it would install the ProcessEvent hook for
// nothing.
//
// Addresses and offsets are valid only for the executable accepted by
// ClientExecutableGuard.
class ClientSpectatorNameplatesPatch : public HookBase<
	void(__fastcall*)(void*, void*),
	0x113AA6F0,
	ClientSpectatorNameplatesPatch> {
public:
	using Base = HookBase<
		void(__fastcall*)(void*, void*),
		0x113AA6F0,
		ClientSpectatorNameplatesPatch>;

	// Install() comes from HookBase — this hook is unconditional for a
	// supported executable and needs no extra gating at attach time.
	static void __fastcall Call(void* hud, void* edx);
	static void __fastcall CallOriginal(void* hud, void* edx) {
		m_original(hud, edx);
	}
};
