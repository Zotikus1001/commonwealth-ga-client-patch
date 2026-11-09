#pragma once

// GLOBAL CLIENT PATCH: Foot Placement Off
// Applies to LIVE and private servers in every client DLL build; players can disable it with
// [Commonwealth.ClientPatch] FootPlacementOff=0 in TgEngine.ini.
//
// Every frame the native pawn tick (0x109E15A0) asks 0x109DA5B0 whether foot placement may run; when it may, both
// SkelControlFootPlacement legs are enabled and the worker at 0x109E0760 traces the ground under each foot
// (collision line checks per pawn per frame, a large share of the game thread in robot crowds). The hook keeps the
// decision function's work but answers "no", which disables the leg controls and skips the worker. Feet then
// follow the animation instead of adapting to slopes and steps. Addresses are Ghidra-verified for this client.

#include "src/pch.hpp"

class ClientFootPlacementPatch {
public:
	static LONG Install();
};
