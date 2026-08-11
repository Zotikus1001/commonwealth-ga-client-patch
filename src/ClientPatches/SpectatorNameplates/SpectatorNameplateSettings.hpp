#pragma once

// Hot-reloaded tuning for the spectator nameplates.
//
// Values live in SpectatorNameplates.ini next to the game executable and are
// re-read about once a second, so they can be edited while the game runs and
// take effect on alt-tab. Deliberately a plain Win32 profile file rather than
// the engine's config system: nothing here needs to survive as a user setting
// or appear in the options UI yet, and this keeps the patch free of the
// engine's config natives while the values are still being chosen.
//
// Missing file or missing key falls back to the documented default, so the
// patch behaves correctly with no ini present at all.
//
//   [Nameplates]
//   ScalePercent=160     ; text scale, percent. 100 = engine default size
//   HeightOffset=55      ; world units above the pawn's cylinder centre
//   ShadowAlpha=200      ; 0 disables the drop shadow
//   MaxDistance=0        ; world units; 0 = no distance limit
struct SpectatorNameplateSettings {
	float scale        = 1.6f;
	float heightOffset = 55.0f;
	int   shadowAlpha  = 200;
	float maxDistance   = 0.0f;
	// 0 = Canvas->Font (whatever the HUD last set), 1 = ATgHUD::m_OverheadFont,
	// 2 = ATgHUD::m_OverheadFont2. The overhead fonts are the ones the game
	// uses for its own above-pawn text, so they are built for this job and are
	// larger natively — preferable to scaling the small default font up, which
	// just makes it soft.
	int   font          = 1;
	// 0 = off, 1 = drawn bars, 2 = numeric text under the name.
	// Bars use a low-level 2D line renderer whose colour format is inferred
	// from the decompile; 2 is the fallback if they ever misbehave, since it
	// reuses the same text path as the name itself.
	int   healthDisplay = 1;
	int   barWidth      = 60;   // pixels at 100% scale
	int   barHeight     = 5;
	// Power is replicated owner-only by the server (see the bNetOwner gate on
	// r_fCurrentPowerPool), so a spectator receives an initial value and no
	// updates. Hidden by default: a bar frozen at a stale value is worse than
	// no bar. Set ShowPowerBar=1 if the server ever broadcasts it.
	bool  showPowerBar  = false;
	// Bar colours as 0-255 RGB. Health lerps from Empty to Full as it drops.
	int   healthFullR = 40,  healthFullG = 230, healthFullB = 60;
	int   healthLowR  = 235, healthLowG  = 45,  healthLowB  = 40;
	int   powerR      = 40,  powerG      = 140, powerB      = 255;
	int   backdropAlpha = 165;  // dark backing behind each bar, 0-255

	// Returns the current values, re-reading the ini if the poll interval has
	// elapsed. Cheap to call every frame.
	static const SpectatorNameplateSettings& Get();
};
