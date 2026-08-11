#include "src/ClientPatches/UI/SpectatorNameplates/SpectatorNameplateDraw.hpp"

#include "src/ClientPatches/UI/CombatTextScale/CombatTextScalePatch.hpp"
#include "src/ClientPatches/UI/SpectatorNameplates/SpectatorNameplateFormat.hpp"
#include "src/ClientPatches/UI/SpectatorNameplates/SpectatorNameplateSettings.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

#ifdef GA_CLIENT_DEBUG
#include "src/Utils/Logger/Logger.hpp"
#include <windows.h>
#endif

namespace {

// ── Engine entry points, valid only for the guarded executable ──────────────

// UCanvas::Project. Confirmed from DrawActorOverlays @0x113AA6F0, which calls
// it as Project(&out, Canvas, loc.X, loc.Y, loc.Z) and then uses ClipX/ClipY to
// centre the result — i.e. it already returns screen pixels, not NDC.
// Returns zeros when the canvas has no scene view (Canvas+0x70 null).
using ProjectFn = void(__cdecl*)(float* outXYZ, void* canvas, float x, float y, float z);
constexpr std::uintptr_t kProjectAddr = 0x109C8600u;

// The wrapped-text renderer behind UCanvas::DrawText. Thiscall, Canvas in ECX.
// Signature recovered from execDrawText @0x10ED6760, which calls it as
//   (bDraw=1, &outXL, &outYL, Canvas->Font, scaleX, scaleY, bCentered, text)
// It reads CurX/CurY as the pen position and OrgX/OrgY as the origin.
using DrawTextFn = void(__thiscall*)(
	void* canvas, int bDraw, int* outXL, int* outYL, void* font,
	float scaleX, float scaleY, int bCentered, const wchar_t* text);
constexpr std::uintptr_t kDrawTextAddr = 0x10ED5C20u;

// ── Field views ────────────────────────────────────────────────────────────

struct FStringView {
	const wchar_t* Data;
	int            Count;
	int            Max;
};

struct HudView {
	std::uint8_t reserved0[0x0D8];
	void*        WorldInfo;
	std::uint8_t reserved1[0x42C - 0x0DC];
	void*        Canvas;
	std::uint8_t reserved2[0x464 - 0x430];
	void*        m_PlayerOwner;    // ATgHUD::m_PlayerOwner
	std::uint8_t reserved3[0x474 - 0x468];
	void*        m_OverheadFont;   // ATgHUD — the game's own overhead text font
	void*        m_OverheadFont2;
};
static_assert(offsetof(HudView, WorldInfo)       == 0x0D8, "AActor::WorldInfo");
static_assert(offsetof(HudView, Canvas)          == 0x42C, "AHUD::Canvas");
static_assert(offsetof(HudView, m_PlayerOwner)   == 0x464, "ATgHUD::m_PlayerOwner");
static_assert(offsetof(HudView, m_OverheadFont)  == 0x474, "ATgHUD::m_OverheadFont");
static_assert(offsetof(HudView, m_OverheadFont2) == 0x478, "ATgHUD::m_OverheadFont2");

struct CanvasView {
	std::uint8_t  reserved0[0x03C];
	void*         Font;      // UCanvas::Font
	float         OrgX;
	float         OrgY;
	float         ClipX;
	float         ClipY;
	float         CurX;      // pen position used by the text renderer
	float         CurY;
	std::uint8_t  reserved1[0x05C - 0x058];
	std::uint8_t  DrawColorB; // FColor is B,G,R,A on this build
	std::uint8_t  DrawColorG;
	std::uint8_t  DrawColorR;
	std::uint8_t  DrawColorA;
	std::uint8_t  reserved2[0x06C - 0x060];
	void*         RenderTarget;  // dereferenced by the text renderer
	void*         SceneView;     // Project returns zeros when this is null
};
static_assert(offsetof(CanvasView, Font)         == 0x03C, "UCanvas::Font");
static_assert(offsetof(CanvasView, OrgX)         == 0x040, "UCanvas::OrgX");
static_assert(offsetof(CanvasView, ClipX)        == 0x048, "UCanvas::ClipX");
static_assert(offsetof(CanvasView, ClipY)        == 0x04C, "UCanvas::ClipY");
static_assert(offsetof(CanvasView, CurX)         == 0x050, "UCanvas::CurX");
static_assert(offsetof(CanvasView, CurY)         == 0x054, "UCanvas::CurY");
static_assert(offsetof(CanvasView, DrawColorB)   == 0x05C, "UCanvas::DrawColor");
static_assert(offsetof(CanvasView, RenderTarget) == 0x06C, "UCanvas render target");
static_assert(offsetof(CanvasView, SceneView)    == 0x070, "UCanvas scene view");

template <typename T>
struct TArrayView {
	T*  Data;
	int Count;
	int Max;
};

struct GameReplicationInfoView {
	std::uint8_t             reserved0[0x244];
	TArrayView<void*>        PRIArray;
};
static_assert(offsetof(GameReplicationInfoView, PRIArray) == 0x244,
	"AGameReplicationInfo::PRIArray");

struct WorldInfoView {
	std::uint8_t reserved0[0x32C];
	void*        GRI;
};
static_assert(offsetof(WorldInfoView, GRI) == 0x32C, "AWorldInfo::GRI");

struct RepInfoPlayerView {
	std::uint8_t  reserved0[0x1E0];
	FStringView   PlayerName;        // APlayerReplicationInfo::PlayerName
	std::uint8_t  reservedA[0x210 - 0x1EC];
	std::uint32_t flags;             // bit 7 bBot
	std::uint8_t  reservedB[0x290 - 0x214];
	void*         r_TaskForce;
	std::uint8_t  reserved2[0x668 - 0x294];
	FStringView   r_sOrigPlayerName; // TgRepInfo_Player
	std::uint8_t  reserved3[0x690 - 0x674];
	void*         r_PawnOwner;
};
static_assert(offsetof(RepInfoPlayerView, PlayerName)        == 0x1E0, "PlayerName");
static_assert(offsetof(RepInfoPlayerView, flags)             == 0x210, "PRI flag word");
static_assert(offsetof(RepInfoPlayerView, r_TaskForce)       == 0x290, "r_TaskForce");
static_assert(offsetof(RepInfoPlayerView, r_sOrigPlayerName) == 0x668, "r_sOrigPlayerName");
static_assert(offsetof(RepInfoPlayerView, r_PawnOwner)       == 0x690, "r_PawnOwner");

// Which name field actually carries text.
//
// r_sOrigPlayerName is only ever written by TgRepInfo_Player.SetOrigPlayerName,
// which this server never calls — measured empty for every PRI in a live
// instance (named=0 across 66 PRIs). The populated field is the base engine
// PlayerName, which is what the scoreboard and existing HUD tags display.
// Prefer it, and fall back to r_sOrigPlayerName in case a future server
// populates that instead.
const FStringView* ResolveName(RepInfoPlayerView* pri) {
	auto valid = [](const FStringView& value) {
		return value.Data &&
			SpectatorNameplateFormat::IsValidNameCount(value.Count, value.Max) &&
			value.Data[value.Count - 1] == L'\0';
	};
	if (valid(pri->PlayerName)) return &pri->PlayerName;
	if (valid(pri->r_sOrigPlayerName)) {
		return &pri->r_sOrigPlayerName;
	}
	return nullptr;
}

struct TaskForceView {
	std::uint8_t  reserved0[0x1FC];
	std::uint8_t  r_nTaskForce;  // 1 = attackers, 2 = defenders
};
static_assert(offsetof(TaskForceView, r_nTaskForce) == 0x1FC, "r_nTaskForce");

struct ActorView {
	std::uint8_t reserved0[0x54];
	float        LocationX;
	float        LocationY;
	float        LocationZ;
	int          RotationPitch;   // AActor::Rotation
	int          RotationYaw;
	int          RotationRoll;
	std::uint8_t reserved1[0x2C4 - 0x6C];
	int          Health;          // APawn::Health
	std::uint8_t reserved2[0x43C - 0x2C8];
	int          r_nHealthMaximum;  // ATgPawn runtime maximum; APawn::HealthMax
	                                // can remain at the stale class default.
	std::uint8_t reserved3[0x109C - 0x440];
	float        r_fCurrentPowerPool;
	float        r_fMaxPowerPool;
};
static_assert(offsetof(ActorView, LocationX)           == 0x54,   "AActor::Location");
static_assert(offsetof(ActorView, RotationPitch)       == 0x60,   "AActor::Rotation");
static_assert(offsetof(ActorView, Health)              == 0x2C4,  "APawn::Health");
static_assert(offsetof(ActorView, r_nHealthMaximum)    == 0x43C,  "ATgPawn::r_nHealthMaximum");
static_assert(offsetof(ActorView, r_fCurrentPowerPool) == 0x109C, "ATgPawn::r_fCurrentPowerPool");
static_assert(offsetof(ActorView, r_fMaxPowerPool)     == 0x10A0, "ATgPawn::r_fMaxPowerPool");

// APlayerController::PlayerCamera -> ACamera::CameraCache.POV. This is the view
// actually being rendered, including while following a pawn, so it is a better
// source than the controller's own transform.
struct PlayerControllerCameraView {
	std::uint8_t reserved0[0x358];
	void*        PlayerCamera;
};
static_assert(offsetof(PlayerControllerCameraView, PlayerCamera) == 0x358,
	"APlayerController::PlayerCamera");

struct CameraView {
	std::uint8_t reserved0[0x2A0];
	float        CacheTimeStamp;   // FTCameraCache::TimeStamp
	float        PovX;             // FTCameraCache::POV.Location
	float        PovY;
	float        PovZ;
	int          PovPitch;         // FTCameraCache::POV.Rotation
	int          PovYaw;
	int          PovRoll;
};
static_assert(offsetof(CameraView, PovX)     == 0x2A4, "Camera POV Location");
static_assert(offsetof(CameraView, PovPitch) == 0x2B0, "Camera POV Rotation");

struct Vec3 { float x, y, z; };

// Where the frame is being viewed from, and which way. Prefers the camera's
// cached POV; falls back to the controller's own transform if no camera exists
// yet. Returns false when neither is available, in which case the caller must
// not apply direction or distance culling.
bool CameraPov(void* playerController, Vec3& outPos, Vec3& outDir) {
	if (!playerController) return false;

	auto* pcCam = static_cast<PlayerControllerCameraView*>(playerController);
	int pitch = 0, yaw = 0;

	if (pcCam->PlayerCamera) {
		auto* cam = static_cast<CameraView*>(pcCam->PlayerCamera);
		outPos = {cam->PovX, cam->PovY, cam->PovZ};
		pitch = cam->PovPitch;
		yaw   = cam->PovYaw;
	} else {
		auto* actor = static_cast<ActorView*>(playerController);
		outPos = {actor->LocationX, actor->LocationY, actor->LocationZ};
		pitch = actor->RotationPitch;
		yaw   = actor->RotationYaw;
	}

	// UE3 rotator units: 65536 per full turn.
	constexpr float kToRadians = 6.28318530718f / 65536.0f;
	const float p = static_cast<float>(pitch) * kToRadians;
	const float y = static_cast<float>(yaw)   * kToRadians;
	const float cosP = std::cos(p);
	outDir = {cosP * std::cos(y), cosP * std::sin(y), std::sin(p)};
	return true;
}

// Plate tuning is hot-reloaded from cconfig\SpectatorNameplates.ini; the
// combat-text slider is applied as a runtime multiplier.

// bBot on APlayerReplicationInfo. AI pawns (Minion Android, Support Widow,
// Boss Viking, turrets, and anything else with a controller) get a PRI in
// PRIArray exactly like players do, so the array alone is not a player list.
// This is the same discriminator the server itself uses to count real players
// (UObject__ProcessEvent.cpp: `if (PRI == nullptr || PRI->bBot) continue;`).
constexpr std::uint32_t kBitBot = 0x00000080;


// The low-level 2D line draw behind UCanvas::Draw2DLine. Reached from
// execDraw2DLine @0x10ED7480, which resolves it as
//   (Canvas->RenderTarget, {x1,y1}, {x2,y2}, FLinearColor*)
// after adding OrgX/OrgY to both endpoints. Chosen over DrawTile because it
// needs no texture and no UV rectangle — nothing here has to guess at the
// layout of a HUD atlas.
using Draw2DLineFn = void(__cdecl*)(
	void* renderTarget, const float* start2, const float* end2, const float* colorRgba4);
constexpr std::uintptr_t kDraw2DLineAddr = 0x10ED4210u;

// Filled rectangle as a stack of 1px horizontal lines. Bars are only a few
// pixels tall, so this is a handful of calls and avoids needing a tile/texture
// path at all.
void FillRect(CanvasView* canvas, float x, float y, float w, float h,
              float r, float g, float b, float a) {
	if (!(w > 0.0f) || !(h > 0.0f)) return;
	const float left   = x > 0.0f ? x : 0.0f;
	const float right  = x + w < canvas->ClipX ? x + w : canvas->ClipX;
	const float top    = y > 0.0f ? y : 0.0f;
	const float bottom = y + h < canvas->ClipY ? y + h : canvas->ClipY;
	if (!(right > left) || !(bottom > top)) return;

	const auto drawLine = reinterpret_cast<Draw2DLineFn>(kDraw2DLineAddr);
	const float colour[4] = {r, g, b, a};
	int rows = static_cast<int>(std::ceil(bottom - top));
	if (rows > SpectatorNameplateFormat::kFillRectRowMax) {
		rows = SpectatorNameplateFormat::kFillRectRowMax;
	}
	for (int i = 0; i < rows; ++i) {
		const float ly = top + static_cast<float>(i);
		const float start[2] = {canvas->OrgX + left,  canvas->OrgY + ly};
		const float end[2]   = {canvas->OrgX + right, canvas->OrgY + ly};
		drawLine(canvas->RenderTarget, start, end, colour);
	}
}

struct Rgb { std::uint8_t r, g, b; };

// Colour interpolation and stat-line formatting live in the header so
// tests/spectator_nameplate_format_test.cpp can exercise them off-game.
using SpectatorNameplateFormat::BarFraction;
using SpectatorNameplateFormat::Clamp01;
using SpectatorNameplateFormat::FormatStats;
using SpectatorNameplateFormat::Lerp255;
using SpectatorNameplateFormat::MultiplyScale;
using SpectatorNameplateFormat::RoundedStat;
using SpectatorNameplateFormat::SafeArrayCount;
using SpectatorNameplateFormat::ScaledBarHeight;

// Attackers red, defenders blue, unknown white — matching the scoreboard's
// own sense of the two sides rather than inventing a new scheme.
Rgb TeamColour(RepInfoPlayerView* pri) {
	auto* tf = static_cast<TaskForceView*>(pri->r_TaskForce);
	if (!tf) return {255, 255, 255};
	switch (tf->r_nTaskForce) {
		case 1:  return {255,  80,  80};
		case 2:  return {110, 160, 255};
		default: return {255, 255, 255};
	}
}

}  // namespace

namespace SpectatorNameplateDraw {

void Render(void* hudPointer) {
	auto* hud = static_cast<HudView*>(hudPointer);
	if (!hud) return;

	auto* canvas = static_cast<CanvasView*>(hud->Canvas);
	auto* world  = static_cast<WorldInfoView*>(hud->WorldInfo);

#ifdef GA_CLIENT_DEBUG
	// Throttled to ~1/sec. Reports every precondition and the per-frame tally,
	// so one live session shows exactly where the draw stops. Without this the
	// early-outs below are indistinguishable from each other.
	static DWORD s_lastReport = 0;
	const DWORD  now = GetTickCount();
	const bool   report = (now - s_lastReport) >= 1000;
	int dbgPris = 0, dbgWithPawn = 0, dbgNamed = 0, dbgOnScreen = 0, dbgDrawn = 0;
	struct Reporter {
		bool on; CanvasView* c; WorldInfoView* w;
		int *pris, *pawns, *named, *onScreen, *drawn;
		const char* stop;
		~Reporter() {
			if (!on) return;
			Logger::Log("clientpatch",
				"[spectator-nameplates] draw: stop=%s canvas=%p sceneView=%p rt=%p font=%p "
				"clip=%.0fx%.0f gri=%p pris=%d withPawn=%d named=%d onScreen=%d drawn=%d\n",
				stop, c, c ? c->SceneView : nullptr, c ? c->RenderTarget : nullptr,
				c ? c->Font : nullptr, c ? c->ClipX : 0.0f, c ? c->ClipY : 0.0f,
				w ? w->GRI : nullptr, *pris, *pawns, *named, *onScreen, *drawn);
		}
	} rep{report, canvas, world, &dbgPris, &dbgWithPawn, &dbgNamed, &dbgOnScreen,
	      &dbgDrawn, "ok"};
	if (report) s_lastReport = now;
#define NP_STOP(reason) do { rep.stop = (reason); } while (0)
#else
#define NP_STOP(reason) do { } while (0)
#endif

	if (!canvas || !world) { NP_STOP("no-canvas-or-world"); return; }

	// Project needs the scene view, and the text renderer dereferences the
	// render target. Both are non-null only mid-render; bail rather than risk
	// it if we are ever called outside that window.
	if (!canvas->SceneView)    { NP_STOP("no-sceneview");    return; }
	if (!canvas->RenderTarget) { NP_STOP("no-rendertarget"); return; }
	if (!canvas->Font)         { NP_STOP("no-font");         return; }
	if (canvas->ClipX <= 0.0f || canvas->ClipY <= 0.0f) { NP_STOP("no-clip"); return; }

	auto* gri = world->GRI ? static_cast<GameReplicationInfoView*>(world->GRI) : nullptr;
	if (!gri)                    { NP_STOP("no-gri");      return; }
	if (!gri->PRIArray.Data)     { NP_STOP("no-priarray"); return; }
	if (gri->PRIArray.Count <= 0){ NP_STOP("empty-pris");  return; }
	const int priCount = SafeArrayCount(gri->PRIArray.Count, gri->PRIArray.Max);
	if (priCount <= 0)           { NP_STOP("invalid-pris"); return; }

	const auto project  = reinterpret_cast<ProjectFn>(kProjectAddr);
	const auto drawText = reinterpret_cast<DrawTextFn>(kDrawTextAddr);
	const SpectatorNameplateSettings& cfg = SpectatorNameplateSettings::Get();
	const float drawScale = MultiplyScale(
		cfg.scale,
		static_cast<float>(ClientCombatTextScalePatch::ScalePercent()) / 100.0f);

	// Camera POV drives both the behind-camera reject and MaxDistance. Without
	// it neither cull can be applied, so both are skipped rather than guessed.
	Vec3 camPos{}, camDir{};
	const bool haveCamera = CameraPov(hud->m_PlayerOwner, camPos, camDir);
	const float maxDistSq = cfg.maxDistance * cfg.maxDistance;

	// Font choice. The overhead fonts belong to the HUD, not the Canvas, and
	// either may be null on a HUD that has not drawn overhead text yet — fall
	// back to the Canvas font rather than passing null to the renderer, which
	// treats a null font as a fatal "DrawText: No font".
	void* font = canvas->Font;
	if (cfg.font == 1 && hud->m_OverheadFont)  font = hud->m_OverheadFont;
	if (cfg.font == 2 && hud->m_OverheadFont2) font = hud->m_OverheadFont2;
	if (!font) { NP_STOP("no-font"); return; }

	// Preserve the pen and colour: the engine keeps drawing with this Canvas
	// after we return, and CurX/CurY are stateful.
	const float        savedCurX = canvas->CurX;
	const float        savedCurY = canvas->CurY;
	const std::uint8_t savedB    = canvas->DrawColorB;
	const std::uint8_t savedG    = canvas->DrawColorG;
	const std::uint8_t savedR    = canvas->DrawColorR;
	const std::uint8_t savedA    = canvas->DrawColorA;

	for (int i = 0; i < priCount; ++i) {
		auto* pri = static_cast<RepInfoPlayerView*>(gri->PRIArray.Data[i]);
		if (!pri) continue;
		if (pri->flags & kBitBot) continue;  // AI, not a player
#ifdef GA_CLIENT_DEBUG
		++dbgPris;
#endif

		auto* pawn = static_cast<ActorView*>(pri->r_PawnOwner);
		if (!pawn) continue;  // dead, unspawned, or another spectator
#ifdef GA_CLIENT_DEBUG
		++dbgWithPawn;
#endif

		const FStringView* namePtr = ResolveName(pri);
		if (!namePtr) continue;
		const FStringView& name = *namePtr;
#ifdef GA_CLIENT_DEBUG
		++dbgNamed;
#endif

		// Behind-camera reject. Project passes its Z through untransformed, so
		// a point behind the viewer still yields a positive Z and lands
		// somewhere plausible on screen — testing Z is not enough. Cull before
		// calling the engine projector so off-camera PRIs stay cheap.
		if (haveCamera) {
			const float dx = pawn->LocationX - camPos.x;
			const float dy = pawn->LocationY - camPos.y;
			const float dz = pawn->LocationZ - camPos.z;
			if (dx * camDir.x + dy * camDir.y + dz * camDir.z <= 0.0f) continue;
			if (maxDistSq > 0.0f && (dx * dx + dy * dy + dz * dz) > maxDistSq) continue;
		}

		float screen[3] = {0.0f, 0.0f, 0.0f};
		project(screen, canvas,
			pawn->LocationX, pawn->LocationY, pawn->LocationZ + cfg.heightOffset);
		if (!std::isfinite(screen[0]) || !std::isfinite(screen[1])) continue;

#ifdef GA_CLIENT_DEBUG
		if (report) {
			Logger::Log("clientpatch",
				"[spectator-nameplates] plate: name=%S pawn=%p loc=(%.0f,%.0f,%.0f) "
				"screen=(%.0f,%.0f,%.1f)\n",
				name.Data, pawn, pawn->LocationX, pawn->LocationY, pawn->LocationZ,
				screen[0], screen[1], screen[2]);
		}
#endif

		if (screen[0] < 0.0f || screen[0] > canvas->ClipX) continue;
		if (screen[1] < 0.0f || screen[1] > canvas->ClipY) continue;
#ifdef GA_CLIENT_DEBUG
		++dbgOnScreen;
#endif

		// Measure first (bDraw=0) so the plate can be centred on the pawn.
		// The renderer's own bCentered flag centres against ClipX — the whole
		// screen — which is not what we want.
		int outXL = 0;
		int outYL = 0;
		canvas->CurX = 0.0f;
		canvas->CurY = 0.0f;
		drawText(canvas, /*bDraw=*/0, &outXL, &outYL, font,
			drawScale, drawScale, /*bCentered=*/0, name.Data);

		const float textX = screen[0] - static_cast<float>(outXL) * 0.5f;
		const float textY = screen[1] - static_cast<float>(outYL);

		// Shadow first, then the coloured text one pixel up-left. Cheap
		// outline: the default font is thin and vanishes against the bright
		// skyboxes and floodlit interiors this game is full of.
		int shadowXL = 0;
		int shadowYL = 0;
		canvas->DrawColorR = 0;
		canvas->DrawColorG = 0;
		canvas->DrawColorB = 0;
		canvas->DrawColorA = static_cast<std::uint8_t>(cfg.shadowAlpha);
		canvas->CurX = textX + 1.0f;
		canvas->CurY = textY + 1.0f;
		if (cfg.shadowAlpha > 0) {
			drawText(canvas, /*bDraw=*/1, &shadowXL, &shadowYL, font,
				drawScale, drawScale, /*bCentered=*/0, name.Data);
		}

		const Rgb colour = TeamColour(pri);
		canvas->DrawColorR = colour.r;
		canvas->DrawColorG = colour.g;
		canvas->DrawColorB = colour.b;
		canvas->DrawColorA = 255;
		canvas->CurX = textX;
		canvas->CurY = textY;
		drawText(canvas, /*bDraw=*/1, &outXL, &outYL, font,
			drawScale, drawScale, /*bCentered=*/0, name.Data);

		// Health and power beneath the name. Max values can legitimately be 0
		// on a pawn that has not finished initialising, so guard every divide.
		if (cfg.healthDisplay != 0) {
			const int   hp    = pawn->Health;
			const int   hpMax = pawn->r_nHealthMaximum;
			const float pw    = pawn->r_fCurrentPowerPool;
			const float pwMax = pawn->r_fMaxPowerPool;

			const float hpFrac = BarFraction(static_cast<float>(hp),
			                                 static_cast<float>(hpMax));
			const float pwFrac = BarFraction(pw, pwMax);

			if (cfg.healthDisplay == 1) {
				const float barW = static_cast<float>(cfg.barWidth) * drawScale;
				const float barH = ScaledBarHeight(cfg.barHeight, drawScale);
				const float barX = screen[0] - barW * 0.5f;
				float       barY = textY + static_cast<float>(outYL) + 2.0f;

				// Colours come from the ini so they can be tuned live. Health
				// lerps between the Low and Full colours as it drains; power is
				// flat. Each bar gets a dark backing so it reads against a
				// bright skybox.
				const float back = static_cast<float>(cfg.backdropAlpha) / 255.0f;
				const float hpR = Lerp255(cfg.healthLowR, cfg.healthFullR, hpFrac);
				const float hpG = Lerp255(cfg.healthLowG, cfg.healthFullG, hpFrac);
				const float hpB = Lerp255(cfg.healthLowB, cfg.healthFullB, hpFrac);

				FillRect(canvas, barX - 1.0f, barY - 1.0f, barW + 2.0f, barH + 2.0f,
					0.0f, 0.0f, 0.0f, back);
				FillRect(canvas, barX, barY, barW * hpFrac, barH, hpR, hpG, hpB, 1.0f);

				if (cfg.showPowerBar) {
					barY += barH + 3.0f;
					FillRect(canvas, barX - 1.0f, barY - 1.0f, barW + 2.0f, barH + 2.0f,
						0.0f, 0.0f, 0.0f, back);
					FillRect(canvas, barX, barY, barW * pwFrac, barH,
						static_cast<float>(cfg.powerR) / 255.0f,
						static_cast<float>(cfg.powerG) / 255.0f,
						static_cast<float>(cfg.powerB) / 255.0f, 1.0f);
				}
			} else {
				wchar_t stats[64];
				FormatStats(stats, 64, hp, hpMax,
					RoundedStat(pw), RoundedStat(pwMax),
					cfg.showPowerBar);
				int statsXL = 0, statsYL = 0;
				canvas->CurX = 0.0f;
				canvas->CurY = 0.0f;
				drawText(canvas, /*bDraw=*/0, &statsXL, &statsYL, font,
					drawScale, drawScale, /*bCentered=*/0, stats);
				canvas->DrawColorR = 235;
				canvas->DrawColorG = 235;
				canvas->DrawColorB = 235;
				canvas->DrawColorA = 255;
				canvas->CurX = screen[0] - static_cast<float>(statsXL) * 0.5f;
				canvas->CurY = textY + static_cast<float>(outYL);
				drawText(canvas, /*bDraw=*/1, &statsXL, &statsYL, font,
					drawScale, drawScale, /*bCentered=*/0, stats);
			}
		}
#ifdef GA_CLIENT_DEBUG
		++dbgDrawn;
		if (report && dbgDrawn == 1) {
			Logger::Log("clientpatch",
				"[spectator-nameplates] first: name=%S screen=(%.0f,%.0f,%.1f) "
				"loc=(%.0f,%.0f,%.0f) extent=%dx%d\n",
				name.Data, screen[0], screen[1], screen[2],
				pawn->LocationX, pawn->LocationY, pawn->LocationZ, outXL, outYL);
		}
#endif
	}

	canvas->CurX       = savedCurX;
	canvas->CurY       = savedCurY;
	canvas->DrawColorB = savedB;
	canvas->DrawColorG = savedG;
	canvas->DrawColorR = savedR;
	canvas->DrawColorA = savedA;
}

}  // namespace SpectatorNameplateDraw
