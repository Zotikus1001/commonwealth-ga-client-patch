#pragma once

#include "src/Utils/HookBase.hpp"

// SPECTATOR OVERHEAD OVERLAYS
//
// TgHUD_Game::DrawActorOverlays is called with a live Canvas for both players
// and spectators, but its native pawn path produces no overhead plates for a
// spectator. This hook uses that render boundary to draw spectator plates from
// the already-replicated player and pawn state.
//
// WHAT THIS DOES
// For spectator HUDs only, draw the custom nameplates before passing through to
// the original. Non-spectators are passed through untouched.
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
