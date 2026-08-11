#include "src/ClientPatches/SpectatorNameplates/SpectatorNameplatesPatch.hpp"

#include "src/ClientPatches/SpectatorNameplates/SpectatorNameplateDraw.hpp"

#include <cstddef>
#include <cstdint>

#ifdef GA_CLIENT_DEBUG
#include "src/Utils/Logger/Logger.hpp"
#endif

namespace {

// Private ABI views. Only the fields this patch reads or writes are modeled;
// each offset was taken from the owning class in the generated SDK headers for
// the reviewed executable and cross-checked against the DrawActorOverlays
// decompile, not inferred from one or the other alone.

struct HudView {
	void**        vtable;
	std::uint8_t  reserved0[0x0D8 - 0x004];
	void*         WorldInfo;       // AActor::WorldInfo
	std::uint8_t  reserved1[0x42C - 0x0DC];
	void*         Canvas;          // AHUD::Canvas
	std::uint8_t  reserved2[0x464 - 0x430];
	void*         m_PlayerOwner;   // ATgHUD::m_PlayerOwner
	void*         m_PawnOwner;     // ATgHUD::m_PawnOwner
	std::uint8_t  reserved3[0x480 - 0x46C];
	std::uint32_t postRenderFlags; // ATgHUD: bit 1 m_bDisablePostRender
	std::uint8_t  reserved4[0x678 - 0x484];
	std::uint32_t pawnHudFlags;    // TgHUD_Game: bit 0 m_bDrawPawnHUD,
	                               //             bit 1 m_bBlockPawnHUD
};
static_assert(offsetof(HudView, WorldInfo)       == 0x0D8, "AActor::WorldInfo");
static_assert(offsetof(HudView, Canvas)          == 0x42C, "AHUD::Canvas");
static_assert(offsetof(HudView, m_PlayerOwner)   == 0x464, "ATgHUD::m_PlayerOwner");
static_assert(offsetof(HudView, m_PawnOwner)     == 0x468, "ATgHUD::m_PawnOwner");
static_assert(offsetof(HudView, postRenderFlags) == 0x480, "ATgHUD post-render flags");
static_assert(offsetof(HudView, pawnHudFlags)    == 0x678, "TgHUD_Game pawn-HUD flags");

struct WorldInfoView {
	std::uint8_t reserved0[0x32C];
	void*        GRI;       // AWorldInfo::GRI — the native's final gate
	std::uint8_t reserved1[0x378 - 0x330];
	void*        PawnList;  // AWorldInfo::PawnList, linked via APawn::NextPawn
};
static_assert(offsetof(WorldInfoView, GRI)      == 0x32C, "AWorldInfo::GRI");
static_assert(offsetof(WorldInfoView, PawnList) == 0x378, "AWorldInfo::PawnList");

struct PawnView {
	std::uint8_t reserved0[0x1DC];
	void*        NextPawn;  // APawn::NextPawn
};
static_assert(offsetof(PawnView, NextPawn) == 0x1DC, "APawn::NextPawn");

struct PlayerControllerView {
	std::uint8_t reserved0[0x1D0];
	void*        PlayerReplicationInfo;  // AController::PlayerReplicationInfo
	std::uint8_t reserved1[0x388 - 0x1D4];
	void*        ViewTarget;             // APlayerController::ViewTarget
};
static_assert(offsetof(PlayerControllerView, PlayerReplicationInfo) == 0x1D0,
	"AController::PlayerReplicationInfo");
static_assert(offsetof(PlayerControllerView, ViewTarget) == 0x388,
	"APlayerController::ViewTarget");

struct PlayerReplicationInfoView {
	std::uint8_t  reserved0[0x210];
	std::uint32_t flags;  // APlayerReplicationInfo: bit 3 bOnlySpectator
};
static_assert(offsetof(PlayerReplicationInfoView, flags) == 0x210,
	"APlayerReplicationInfo flag word");

constexpr std::uint32_t kBitDrawPawnHUD   = 0x00000001;
constexpr std::uint32_t kBitBlockPawnHUD  = 0x00000002;
constexpr std::uint32_t kBitOnlySpectator = 0x00000008;

// A spectator is identified by the local PRI's bOnlySpectator, which the server
// sets at login and never clears for a spectating connection.
bool IsSpectatorHud(HudView* hud, PlayerControllerView*& outController) {
	auto* controller = static_cast<PlayerControllerView*>(hud->m_PlayerOwner);
	outController = controller;
	if (!controller) return false;

	auto* pri = static_cast<PlayerReplicationInfoView*>(
		controller->PlayerReplicationInfo);
	if (!pri) return false;

	return (pri->flags & kBitOnlySpectator) != 0;
}

// The pawn to lend the HUD for the duration of the call.
//
// m_PawnOwner is only a GATE in DrawActorOverlays: the camera basis comes from
// the HUD itself, and the loop walks WorldInfo->PawnList (0x378, linked by
// APawn::NextPawn 0x1DC) doing its own frustum test per pawn. So any live pawn
// satisfies it — the overlay pass still covers every pawn in view.
//
// That matters because free-look is the primary spectator case, not an edge
// case. While following a player, ViewTarget is that player's pawn and is the
// natural choice. In free-look the engine points ViewTarget back at the
// controller itself (detected by identity, so no class-name machinery is
// needed), and we fall back to the first entry in the world pawn list purely to
// open the gate.
void* BorrowPawn(PlayerControllerView* controller, HudView* hud) {
	void* viewTarget = controller->ViewTarget;
	if (viewTarget && viewTarget != controller) return viewTarget;

	auto* worldInfo = static_cast<WorldInfoView*>(hud->WorldInfo);
	if (!worldInfo) return nullptr;
	return worldInfo->PawnList;
}

}  // namespace

void __fastcall ClientSpectatorNameplatesPatch::Call(void* hudPointer, void* edx) {
	auto* hud = static_cast<HudView*>(hudPointer);
	if (!hud) {
		CallOriginal(hudPointer, edx);
		return;
	}

	PlayerControllerView* controller = nullptr;
	const bool spectating = IsSpectatorHud(hud, controller);

#ifdef GA_CLIENT_DEBUG
	// One-shot per HUD. Proves the detour fires at all, and shows the spectator
	// determination — without this a false bOnlySpectator is indistinguishable
	// from the hook never running.
	{
		static void* s_seenHud = nullptr;
		if (s_seenHud != hudPointer) {
			s_seenHud = hudPointer;
			auto* pri = controller
				? static_cast<PlayerReplicationInfoView*>(
					controller->PlayerReplicationInfo)
				: nullptr;
			Logger::Log(
				"clientpatch",
				"[spectator-nameplates] entry: hud=%p pc=%p pri=%p "
				"priFlags=0x%08lx spectating=%d viewTarget=%p\n",
				hudPointer, controller, pri,
				pri ? static_cast<unsigned long>(pri->flags) : 0UL,
				spectating ? 1 : 0,
				controller ? controller->ViewTarget : nullptr);
		}
	}
#endif

	if (!spectating) {
		// A real player already satisfies the native's gate chain; never touch
		// the HUD of someone whose overlays already work.
		CallOriginal(hudPointer, edx);
		return;
	}

	void* borrowed = BorrowPawn(controller, hud);
	if (!borrowed) {
		// No pawn anywhere in the world yet — nothing to draw over, and the
		// native would bail on the m_PawnOwner gate regardless.
		CallOriginal(hudPointer, edx);
		return;
	}

	void* const        savedPawn  = hud->m_PawnOwner;
	const std::uint32_t savedFlags = hud->pawnHudFlags;

	hud->m_PawnOwner  = borrowed;
	hud->pawnHudFlags = (savedFlags | kBitDrawPawnHUD) & ~kBitBlockPawnHUD;

#ifdef GA_CLIENT_DEBUG
	// One-shot per HUD: report every gate the native tests, so a single test
	// run identifies which one is still refusing rather than guessing.
	// vtable[0x498] must return 0; it is called here only to observe it, and
	// only in DEBUG builds.
	static void* s_reportedHud = nullptr;
	if (s_reportedHud != hudPointer) {
		s_reportedHud = hudPointer;
		auto* world = static_cast<WorldInfoView*>(hud->WorldInfo);
		using GateFn = int(__fastcall*)(void*, void*);
		const auto gate = reinterpret_cast<GateFn>(hud->vtable[0x498 / sizeof(void*)]);
		const int gateResult = gate ? gate(hudPointer, nullptr) : -1;
		Logger::Log(
			"clientpatch",
			"[spectator-nameplates] gates: world=%p gri=%p canvas=%p "
			"postRenderFlags=0x%08lx vtable498=%d pawnOwner=%p borrowed=%p "
			"pawnHudFlags=0x%08lx->0x%08lx pawnList=%p\n",
			hud->WorldInfo, world ? world->GRI : nullptr, hud->Canvas,
			static_cast<unsigned long>(hud->postRenderFlags), gateResult,
			savedPawn, borrowed,
			static_cast<unsigned long>(savedFlags),
			static_cast<unsigned long>(hud->pawnHudFlags),
			world ? world->PawnList : nullptr);
	}
#endif

	// Draw BEFORE the original, not after.
	//
	// UCanvas::Project takes no view matrix — it uses whatever view transform is
	// current. DrawActorOverlays sets that up on entry and tears it down on exit
	// (its last act is SetDepthTestingEnabled(Canvas, 0)), so calling Project
	// after CallOriginal returns projects against a stale view: measured live,
	// the projected screen positions stayed frozen while the camera moved.
	//
	// The engine's own overhead pass draws nothing for a spectator anyway — its
	// per-pawn half is native and written around having a local pawn — so
	// running first costs nothing and gets a current view.
	SpectatorNameplateDraw::Render(hudPointer);

	CallOriginal(hudPointer, edx);

	// Restore unconditionally: the borrowed state must never be observable
	// outside this call.
	hud->m_PawnOwner  = savedPawn;
	hud->pawnHudFlags = savedFlags;
}
