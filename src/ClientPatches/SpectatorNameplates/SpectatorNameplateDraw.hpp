#pragma once

// Self-drawn spectator nameplates.
//
// The engine's own overhead pass (TgPawn::PostRenderFor -> native
// TGPostRenderFor at pawn vtable+0x498) refuses to draw for a spectator even
// once DrawActorOverlays is unblocked, and its logic is native and written
// around having a local pawn. Rather than fight it, this draws the plates
// directly onto the same Canvas the pass was given.
//
// Everything needed is already replicated to a spectating client and was
// confirmed live before this was written:
//   AWorldInfo::GRI          -> AGameReplicationInfo::PRIArray (+0x244)
//   ATgRepInfo_Player        -> r_TaskForce       (+0x290)
//                               r_sOrigPlayerName (+0x668, FString)
//                               r_PawnOwner       (+0x690)
//   AActor::Location         (+0x054)
//   ATgRepInfo_TaskForce     -> r_nTaskForce      (+0x1FC, byte)
//
// Works identically in free-look and follow-cam, because it never depends on a
// local pawn — which was the whole point.

namespace SpectatorNameplateDraw {

// Draw a nameplate for every live player whose pawn projects on-screen.
// `hud` is the TgHUD_Game the hook was called with; both the Canvas and the
// WorldInfo are read from it. Safe to call with anything null — it no-ops.
void Render(void* hud);

}  // namespace SpectatorNameplateDraw
