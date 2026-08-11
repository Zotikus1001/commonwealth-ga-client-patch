#include "src/ClientPatches/UI/SpectatorNameplates/SpectatorNameplatesPatch.hpp"

#include "src/ClientPatches/UI/SpectatorNameplates/SpectatorNameplateDraw.hpp"

#include <cstddef>
#include <cstdint>

#ifdef GA_CLIENT_DEBUG
#include "src/Utils/Logger/Logger.hpp"
#endif

namespace {

// Private ABI views. Only the fields this patch reads are modeled; each offset
// was verified against the reviewed executable and its generated SDK headers.

struct HudView {
	std::uint8_t  reserved0[0x464];
	void*         m_PlayerOwner;   // ATgHUD::m_PlayerOwner
};
static_assert(offsetof(HudView, m_PlayerOwner)   == 0x464, "ATgHUD::m_PlayerOwner");

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

	// Draw before the original. The canvas projection state is valid here; a
	// live test showed it was stale after the original returned.
	//
	// Do not borrow a pawn or force the native pawn-HUD flags. The custom
	// renderer supplies the spectator plates, while forcing those gates would
	// make the original perform a second pawn-list traversal every frame.
	SpectatorNameplateDraw::Render(hudPointer);

	CallOriginal(hudPointer, edx);
}
