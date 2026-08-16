#include "src/ClientPatches/UI/CombatTextScale/CombatTextScalePatch.hpp"

#include "src/ClientPatches/UI/CombatTextScale/CombatTextScaleSetting.hpp"
#include "src/ClientRuntime/EngineConfig.hpp"
#include "src/ClientRuntime/EngineFont.hpp"
#include "src/Utils/GameWindow/GameWindow.hpp"
#include "src/Utils/Logger/Logger.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <limits>

namespace {

constexpr std::uintptr_t kDrawTextCoreAddress = 0x10ed5c20u;
constexpr std::uintptr_t kDisplayDamageInfoAddress = 0x109dd3a0u;
constexpr std::uintptr_t kCombatFirstShadowReturn = 0x109dd5c7u;
constexpr std::uintptr_t kCombatSecondShadowReturn = 0x109dd60au;
constexpr std::uintptr_t kCombatTextReturn = 0x109dd656u;
constexpr float kCombatFirstShadowBaselineOffset = 21.0f;
constexpr float kCombatSecondShadowBaselineOffset = 23.0f;
constexpr float kCombatTextBaselineOffset = 22.0f;

constexpr std::uintptr_t kBorderedGlyphNameAddress = 0x109dd690u;
constexpr std::uintptr_t kBorderedPlayerNameCallerReturn = 0x109e9d70u;
constexpr std::uintptr_t kBorderedFirstAffiliationCallerReturn = 0x109e9ed5u;
constexpr std::uintptr_t kBorderedSecondAffiliationCallerReturn = 0x109ea03au;
constexpr std::uintptr_t kRawGlyphNameAddress = 0x109d8250u;
constexpr std::uintptr_t kRawPlayerNameCallerReturn = 0x109e9d10u;
constexpr std::uintptr_t kRawFirstAffiliationCallerReturn = 0x109e9e75u;
constexpr std::uintptr_t kRawSecondAffiliationCallerReturn = 0x109e9fdau;
constexpr std::uintptr_t kRawGlyphNameDrawTextReturn = 0x109d82e1u;
constexpr std::uintptr_t kDrawMaterialTileAddress = 0x10ed1030u;
constexpr std::uintptr_t kTargetMarkerDrawReturn = 0x109e7607u;
constexpr std::uintptr_t kLowLevelRawTextAddress = 0x10ed4750u;
constexpr std::uintptr_t kUiStringRawTextReturn = 0x110be610u;
constexpr std::uintptr_t kReticuleUpdateAddress = 0x114ea8d0u;
constexpr float kReferenceViewportHeight = 1080.0f;
constexpr float kMaximumCombatTextScale = 2.0f;
// A one-pixel underdraw restores the retail Prototype digit weight while
// retaining the sharper regular Arial glyph shapes.
constexpr float kReticuleWeightOffset = 1.0f;
constexpr float kTransparentTextColor[4] = {};

constexpr wchar_t kConfigSection[] = L"Commonwealth.ClientPatch";
constexpr wchar_t kConfigValue[] = L"CombatTextScale";

struct CanvasLayout {
	std::uint8_t unknown00[0x3c];
	void* font;
	std::uint8_t unknown40[0x0c];
	float clipY;
	float curX;
	float curY;
	float curYL;
	std::uint32_t unknown5C;
	std::uint32_t flags;
};

struct StringLayout {
	const wchar_t* data;
	int count;
	int capacity;
};

struct CursorState {
	float x;
	float y;
	float lineHeight;
};

struct TextMetrics {
	int width;
	int height;
};

struct CombatContext {
	bool active;
	float baselineY;
};

struct RawNameContext {
	bool active;
	void* referenceFont;
	float retailScale;
	float targetScale;
};

struct ReticuleLayout {
	std::uint8_t unknown00[0x40];
	float healthCurrent;
	std::uint8_t unknown44[0x50 - 0x44];
	float energyCurrent;
};

static_assert(offsetof(CanvasLayout, font) == 0x3c,
	"Unexpected Canvas font offset");
static_assert(offsetof(CanvasLayout, clipY) == 0x4c,
	"Unexpected Canvas ClipY offset");
static_assert(offsetof(CanvasLayout, curX) == 0x50,
	"Unexpected Canvas CurX offset");
static_assert(offsetof(CanvasLayout, curY) == 0x54,
	"Unexpected Canvas CurY offset");
static_assert(offsetof(CanvasLayout, curYL) == 0x58,
	"Unexpected Canvas CurYL offset");
static_assert(offsetof(CanvasLayout, flags) == 0x60,
	"Unexpected Canvas flags offset");
static_assert(sizeof(StringLayout) == 0x0c, "Unexpected FString layout");
static_assert(offsetof(ReticuleLayout, healthCurrent) == 0x40,
	"Unexpected reticule health offset");
static_assert(offsetof(ReticuleLayout, energyCurrent) == 0x50,
	"Unexpected reticule energy offset");
using DrawTextCoreFunction = void(__thiscall*)(
	CanvasLayout*, int, int*, int*, void*, float, float, int,
	const wchar_t*);
using DisplayDamageInfoFunction = void(__fastcall*)(
	void*, void*, CanvasLayout*, float, float, std::uint32_t);
using BorderedGlyphNameFunction = int(__cdecl*)(
	CanvasLayout*, float, float, const StringLayout*, void*, std::uint32_t,
	float);
using RawGlyphNameFunction = BorderedGlyphNameFunction;
using DrawMaterialTileFunction = void(__thiscall*)(
	CanvasLayout*, void*, float, float, float, float,
	float, float, float, float);
using LowLevelRawTextFunction = int(__cdecl*)(
	float*, float, float, const wchar_t*, void*, const void*,
	float, float, float, float*);
using ReticuleUpdateFunction = void(__thiscall*)(ReticuleLayout*, float);
DrawTextCoreFunction g_drawTextCore =
	reinterpret_cast<DrawTextCoreFunction>(kDrawTextCoreAddress);
DisplayDamageInfoFunction g_displayDamageInfo =
	reinterpret_cast<DisplayDamageInfoFunction>(kDisplayDamageInfoAddress);
BorderedGlyphNameFunction g_borderedGlyphName =
	reinterpret_cast<BorderedGlyphNameFunction>(kBorderedGlyphNameAddress);
RawGlyphNameFunction g_rawGlyphName =
	reinterpret_cast<RawGlyphNameFunction>(kRawGlyphNameAddress);
DrawMaterialTileFunction g_drawMaterialTile =
	reinterpret_cast<DrawMaterialTileFunction>(kDrawMaterialTileAddress);
LowLevelRawTextFunction g_lowLevelRawText =
	reinterpret_cast<LowLevelRawTextFunction>(kLowLevelRawTextAddress);
ReticuleUpdateFunction g_reticuleUpdate =
	reinterpret_cast<ReticuleUpdateFunction>(kReticuleUpdateAddress);
std::atomic<int> g_scalePercent{
	CombatTextScaleSetting::kDefaultPercent};
std::atomic<int> g_reticuleHealth{-1};
std::atomic<int> g_reticuleEnergy{-1};
EngineFont::LoadedFontCache g_combatFontCache{"combat-popups"};
EngineFont::LoadedFontCache g_reticuleFontCache{"reticule-pools"};
EngineFont::LoadedFontCache g_reticuleWarningFontCache{"reticule-warnings"};
#ifdef GA_CLIENT_DEBUG
std::atomic_flag g_loggedHealthScale = ATOMIC_FLAG_INIT;
std::atomic_flag g_loggedEnergyScale = ATOMIC_FLAG_INIT;
std::atomic<int> g_loggedCombatFontState{0};
std::atomic<int> g_loggedReticuleFontState{0};
std::atomic<int> g_loggedReticuleWarningFontState{0};
#endif
thread_local CombatContext g_combatContext = {};
thread_local RawNameContext g_rawNameContext = {};

CursorState CaptureCursor(const CanvasLayout& canvas) {
	return {canvas.curX, canvas.curY, canvas.curYL};
}

void RestoreCursor(CanvasLayout& canvas, const CursorState& state) {
	canvas.curX = state.x;
	canvas.curY = state.y;
	canvas.curYL = state.lineHeight;
}

TextMetrics MeasureText(
	CanvasLayout& canvas, void* font, const wchar_t* text) {
	if (!font || !text) return {0, 0};
	const CursorState cursor = CaptureCursor(canvas);
	RestoreCursor(canvas, {0.0f, 0.0f, 0.0f});
	TextMetrics metrics = {};
	g_drawTextCore(
		&canvas, 0, &metrics.width, &metrics.height, font,
		1.0f, 1.0f, 0, text);
	RestoreCursor(canvas, cursor);
	return metrics;
}

bool IsCombatTextCall(std::uintptr_t returnAddress) {
	return returnAddress == kCombatFirstShadowReturn ||
		returnAddress == kCombatSecondShadowReturn ||
		returnAddress == kCombatTextReturn;
}

// The retail helpers are shared by unrelated UI. These six returns are the
// player name and two optional affiliation lines in the friendly label stack.
bool IsRawFriendlyLabelCall(std::uintptr_t returnAddress) {
	return returnAddress == kRawPlayerNameCallerReturn ||
		returnAddress == kRawFirstAffiliationCallerReturn ||
		returnAddress == kRawSecondAffiliationCallerReturn;
}

bool IsBorderedFriendlyLabelCall(std::uintptr_t returnAddress) {
	return returnAddress == kBorderedPlayerNameCallerReturn ||
		returnAddress == kBorderedFirstAffiliationCallerReturn ||
		returnAddress == kBorderedSecondAffiliationCallerReturn;
}

float FriendlyLabelScale(float viewportHeight) {
	if (!std::isfinite(viewportHeight) ||
		viewportHeight <= kReferenceViewportHeight) {
		return 1.0f;
	}
	return kReferenceViewportHeight / viewportHeight;
}

float UserScale() {
	return static_cast<float>(
		g_scalePercent.load(std::memory_order_relaxed)) / 100.0f;
}

float ReticuleVisualScale(float userScale, float viewportHeight) {
	if (userScale <= 1.0f || viewportHeight <= kReferenceViewportHeight) {
		return userScale;
	}
	const float resolutionWeight = std::min(
		(viewportHeight - kReferenceViewportHeight) /
			kReferenceViewportHeight,
		1.0f);
	const float scaleWeight = std::min((userScale - 1.0f) / 0.5f, 1.0f);
	return userScale + 0.2f * resolutionWeight * scaleWeight;
}

#ifdef GA_CLIENT_DEBUG
void LogFontSelection(
	std::atomic<int>& loggedState, const char* surface, void* sourceFont,
	void* replacementFont, float viewportHeight, float drawScale) {
	const int state = replacementFont ? 2 : 1;
	if (loggedState.exchange(state, std::memory_order_relaxed) == state) return;
	Logger::Log(
		"clientpatch",
		"[scaled-text] %s font: source=%s replacement=%s "
		"source-height=%.1f replacement-height=%.1f draw-scale=%.3f\n",
		surface,
		EngineFont::Name(sourceFont) ? EngineFont::Name(sourceFont) : "unknown",
		replacementFont && EngineFont::Name(replacementFont)
			? EngineFont::Name(replacementFont)
			: "retail",
		static_cast<double>(
			EngineFont::EffectiveHeight(sourceFont, viewportHeight)),
		static_cast<double>(replacementFont
			? EngineFont::EffectiveHeight(replacementFont, viewportHeight)
			: 0.0f),
		static_cast<double>(drawScale));
}
#endif

enum class ReticuleText {
	None,
	HealthValue,
	EnergyValue,
	HealthWarning,
	PowerWarning,
};

bool IsReticulePoolValue(ReticuleText text) {
	return text == ReticuleText::HealthValue ||
		text == ReticuleText::EnergyValue;
}

bool IsReticuleWarning(ReticuleText text) {
	return text == ReticuleText::HealthWarning ||
		text == ReticuleText::PowerWarning;
}

int DisplayedReticuleValue(float value) {
	if (!std::isfinite(value) || value < 0.0f ||
		value > static_cast<float>(std::numeric_limits<int>::max())) {
		return -1;
	}
	return static_cast<int>(value);
}

bool ParseUnsignedInteger(const wchar_t* text, int* value) {
	if (!text || !value || *text == L'\0') return false;
	int parsed = 0;
	for (const wchar_t* cursor = text; *cursor != L'\0'; ++cursor) {
		if (*cursor < L'0' || *cursor > L'9') return false;
		const int digit = static_cast<int>(*cursor - L'0');
		if (parsed > (std::numeric_limits<int>::max() - digit) / 10) {
			return false;
		}
		parsed = parsed * 10 + digit;
	}
	*value = parsed;
	return true;
}

bool NormalizedHudPosition(
	float x, float y, float* horizontal, float* vertical) {
	if (!std::isfinite(x) || !std::isfinite(y) ||
		!horizontal || !vertical) {
		return false;
	}
	const GameWindow::ClientSize size = GameWindow::CurrentClientSize();
	if (size.width <= 0 || size.height <= 0) return false;
	const float height = static_cast<float>(size.height);
	*horizontal =
		(x - static_cast<float>(size.width) * 0.5f) / height;
	*vertical = (y - height * 0.5f) / height;
	return true;
}

ReticuleText MatchReticuleText(float x, float y, const wchar_t* text) {
	if (!text) return ReticuleText::None;
	if (std::wcscmp(text, L"Health Low!") == 0) {
		return ReticuleText::HealthWarning;
	}
	if (std::wcscmp(text, L"Power Low!") == 0) {
		return ReticuleText::PowerWarning;
	}

	// This renderer is shared by unrelated numeric UI. Match both the live pool
	// value and its resolution-independent side of the center reticule.
	int value = 0;
	if (!ParseUnsignedInteger(text, &value)) return ReticuleText::None;
	const int health = g_reticuleHealth.load(std::memory_order_relaxed);
	const int energy = g_reticuleEnergy.load(std::memory_order_relaxed);
	if (value != health && value != energy) return ReticuleText::None;

	float horizontal = 0.0f;
	float vertical = 0.0f;
	if (!NormalizedHudPosition(x, y, &horizontal, &vertical) ||
		vertical < 0.0f || vertical > 0.09f) {
		return ReticuleText::None;
	}
	if (horizontal >= -0.17f && horizontal <= -0.03f &&
		value == health) {
		return ReticuleText::HealthValue;
	}
	if (horizontal >= 0.03f && horizontal <= 0.17f &&
		value == energy) {
		return ReticuleText::EnergyValue;
	}
	return ReticuleText::None;
}

int __cdecl LowLevelRawText(
	float* renderContext, float x, float y, const wchar_t* text,
	void* font, const void* color, float scaleX, float scaleY,
	float spacing, float* clip) {
	const std::uintptr_t returnAddress =
		reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	const ReticuleText reticuleText = returnAddress == kUiStringRawTextReturn
		? MatchReticuleText(x, y, text)
		: ReticuleText::None;
	const float userScale = UserScale();
	if (reticuleText != ReticuleText::None && renderContext && font && color &&
		std::isfinite(scaleX) && scaleX > 0.0f &&
		std::isfinite(scaleY) && scaleY > 0.0f &&
		std::isfinite(userScale) && userScale > 0.0f &&
		std::fabs(userScale - 1.0f) >= 0.0001f) {
		const float viewportHeight = static_cast<float>(
			GameWindow::CurrentClientSize().height);
		const float visualScale = ReticuleVisualScale(
			userScale, viewportHeight);
		void* drawFont = font;
		float fontScale = visualScale;
		if (userScale > 1.0f) {
			EngineFont::LoadedFontCache& fontCache =
				IsReticuleWarning(reticuleText)
					? g_reticuleWarningFontCache
					: g_reticuleFontCache;
			if (void* const replacement =
				fontCache.ResolveSharperArial(
					font, viewportHeight, kMaximumCombatTextScale,
					IsReticuleWarning(reticuleText))) {
				const float compensated = EngineFont::CompensatedTextScale(
					font, replacement, viewportHeight, text, visualScale);
				if (std::isfinite(compensated) && compensated > 0.0f) {
					drawFont = replacement;
					fontScale = compensated;
				}
			}
		}
		const int retailWidth = g_lowLevelRawText(
			renderContext, 0.0f, 0.0f, text, font,
			kTransparentTextColor, scaleX, scaleY, spacing, nullptr);
		int targetWidth = g_lowLevelRawText(
			renderContext, 0.0f, 0.0f, text, drawFont,
			kTransparentTextColor, scaleX * fontScale,
			scaleY * fontScale, spacing, nullptr);
		if (targetWidth <= 0 && drawFont != font) {
			drawFont = font;
			fontScale = visualScale;
			targetWidth = g_lowLevelRawText(
				renderContext, 0.0f, 0.0f, text, drawFont,
				kTransparentTextColor, scaleX * fontScale,
				scaleY * fontScale, spacing, nullptr);
		}
		const float weightOffset =
			IsReticulePoolValue(reticuleText) && drawFont != font
			? kReticuleWeightOffset
			: 0.0f;
		const float scaledX = retailWidth > 0 && targetWidth > 0
			? x + (static_cast<float>(retailWidth - targetWidth) -
				weightOffset) * 0.5f
			: x;
#ifdef GA_CLIENT_DEBUG
		std::atomic<int>& loggedFontState = IsReticuleWarning(reticuleText)
			? g_loggedReticuleWarningFontState
			: g_loggedReticuleFontState;
		LogFontSelection(
			loggedFontState,
			IsReticuleWarning(reticuleText)
				? "reticule warnings"
				: "reticule pools",
			font,
			drawFont != font ? drawFont : nullptr,
			viewportHeight, fontScale);
		if (IsReticulePoolValue(reticuleText)) {
			std::atomic_flag& logged =
				reticuleText == ReticuleText::HealthValue
					? g_loggedHealthScale
					: g_loggedEnergyScale;
			if (!logged.test_and_set(std::memory_order_relaxed)) {
				const int displayedValue =
					reticuleText == ReticuleText::HealthValue
						? g_reticuleHealth.load(std::memory_order_relaxed)
						: g_reticuleEnergy.load(std::memory_order_relaxed);
				Logger::Log(
					"clientpatch",
					"[combat-text-scale] reticule %s matched: "
					"value=%d scale=%.2f visual-scale=%.2f\n",
					reticuleText == ReticuleText::HealthValue
						? "health"
						: "energy",
					displayedValue, userScale, visualScale);
			}
		}
#endif
		if (weightOffset > 0.0f) {
			g_lowLevelRawText(
				renderContext, scaledX + weightOffset, y, text,
				drawFont, color, scaleX * fontScale,
				scaleY * fontScale, spacing, clip);
		}
		return g_lowLevelRawText(
			renderContext, scaledX, y, text, drawFont, color,
			scaleX * fontScale, scaleY * fontScale, spacing, clip);
	}
	return g_lowLevelRawText(
		renderContext, x, y, text, font, color,
		scaleX, scaleY, spacing, clip);
}

void __fastcall ReticuleUpdate(
	ReticuleLayout* reticule, void* edx, float timeSeconds) {
	g_reticuleUpdate(reticule, timeSeconds);
	if (!reticule) {
		g_reticuleHealth.store(-1, std::memory_order_relaxed);
		g_reticuleEnergy.store(-1, std::memory_order_relaxed);
		return;
	}
	g_reticuleHealth.store(
		DisplayedReticuleValue(reticule->healthCurrent),
		std::memory_order_relaxed);
	g_reticuleEnergy.store(
		DisplayedReticuleValue(reticule->energyCurrent),
		std::memory_order_relaxed);
}

int __cdecl RawGlyphName(
	CanvasLayout* canvas, float x, float y, const StringLayout* text,
	void* font, std::uint32_t color, float retailScale) {
	const std::uintptr_t returnAddress =
		reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	const RawNameContext previous = g_rawNameContext;
	const float labelScale = canvas ? FriendlyLabelScale(canvas->clipY) : 1.0f;
	if (IsRawFriendlyLabelCall(returnAddress) &&
		std::fabs(labelScale - 1.0f) >= 0.0001f &&
		canvas && font && std::isfinite(retailScale) && retailScale > 0.0f) {
		g_rawNameContext = {
			true, font, retailScale,
			retailScale * labelScale};
	}
	const int result = g_rawGlyphName(
		canvas, x, y, text, font, color, retailScale);
	g_rawNameContext = previous;
	return result;
}

bool DrawNormalizedRawName(
	CanvasLayout& canvas, const StringLayout& text) {
	if (!g_rawNameContext.active || !g_rawNameContext.referenceFont ||
		!canvas.font || !text.data || text.count <= 0 ||
		!std::isfinite(g_rawNameContext.retailScale) ||
		g_rawNameContext.retailScale <= 0.0f ||
		!std::isfinite(g_rawNameContext.targetScale) ||
		g_rawNameContext.targetScale <= 0.0f) {
		return false;
	}

	// The raw helper installs its plain font after the caller has centered the
	// name using the bordered reference font. Recover that center before drawing.
	void* const plainFont = canvas.font;
	const TextMetrics referenceMetrics =
		MeasureText(canvas, g_rawNameContext.referenceFont, text.data);
	const TextMetrics plainMetrics = MeasureText(canvas, plainFont, text.data);
	if (referenceMetrics.width <= 0 || referenceMetrics.height <= 0 ||
		plainMetrics.width <= 0 || plainMetrics.height <= 0) {
		return false;
	}

	const float targetHeight = static_cast<float>(referenceMetrics.height) *
		g_rawNameContext.targetScale;
	const float plainScale =
		targetHeight / static_cast<float>(plainMetrics.height);
	const float retailWidth = static_cast<float>(referenceMetrics.width) *
		g_rawNameContext.retailScale;
	const float retailHeight = static_cast<float>(referenceMetrics.height) *
		g_rawNameContext.retailScale;
	const float targetWidth =
		static_cast<float>(plainMetrics.width) * plainScale;
	if (!std::isfinite(plainScale) || plainScale <= 0.0f ||
		!std::isfinite(retailWidth) || !std::isfinite(retailHeight) ||
		!std::isfinite(targetWidth) || !std::isfinite(targetHeight)) {
		return false;
	}

	canvas.curX += (retailWidth - targetWidth) * 0.5f;
	canvas.curY += (retailHeight - targetHeight) * 0.5f;
	int width = 0;
	int height = 0;
	g_drawTextCore(
		&canvas, 1, &width, &height, plainFont, plainScale, plainScale,
		(canvas.flags & 1u) != 0 ? 1 : 0, text.data);
	return true;
}

void __fastcall DrawMaterialTile(
	CanvasLayout* canvas, void* edx, void* material,
	float x, float y, float width, float height,
	float u, float v, float sizeU, float sizeV) {
	const std::uintptr_t returnAddress =
		reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	if (returnAddress == kTargetMarkerDrawReturn &&
		std::isfinite(width) && width > 0.0f &&
		std::isfinite(height) && height > 0.0f) {
		const float scale = UserScale();
		if (std::isfinite(scale) && scale > 0.0f) {
			x -= width * (scale - 1.0f) * 0.5f;
			y -= height * (scale - 1.0f) * 0.5f;
			width *= scale;
			height *= scale;
		}
	}
	g_drawMaterialTile(
		canvas, material, x, y, width, height, u, v, sizeU, sizeV);
}

float CombatBaselineOffset(std::uintptr_t returnAddress) {
	if (returnAddress == kCombatFirstShadowReturn) {
		return kCombatFirstShadowBaselineOffset;
	}
	if (returnAddress == kCombatSecondShadowReturn) {
		return kCombatSecondShadowBaselineOffset;
	}
	return kCombatTextBaselineOffset;
}

float CombatShadowAdjustment(
	std::uintptr_t returnAddress, float scale) {
	const float adjustment = scale - 1.0f;
	if (returnAddress == kCombatFirstShadowReturn) return adjustment;
	if (returnAddress == kCombatSecondShadowReturn) return -adjustment;
	return 0.0f;
}

bool DrawScaledCombatText(
	CanvasLayout& canvas, const StringLayout& text,
	std::uintptr_t returnAddress) {
	if (!g_combatContext.active || !canvas.font || !text.data ||
		text.count <= 0) {
		return false;
	}

	const int percent = g_scalePercent.load(std::memory_order_relaxed);
	if (percent == CombatTextScaleSetting::kDefaultPercent) return false;
	const float scale = static_cast<float>(percent) / 100.0f;
	const TextMetrics metrics = MeasureText(canvas, canvas.font, text.data);
	if (metrics.width <= 0 || metrics.height <= 0) return false;

	const CursorState initialCursor = CaptureCursor(canvas);
	int ignoredWidth = 0;
	int ignoredHeight = 0;
	g_drawTextCore(
		&canvas, 0, &ignoredWidth, &ignoredHeight, canvas.font,
		1.0f, 1.0f, (canvas.flags & 1u) != 0 ? 1 : 0, text.data);
	const CursorState retailResultCursor = CaptureCursor(canvas);

	RestoreCursor(canvas, initialCursor);
	const float baseline = g_combatContext.baselineY -
		CombatBaselineOffset(returnAddress);
	canvas.curY = baseline - (baseline - initialCursor.y) * scale;

	const float targetHeight = static_cast<float>(metrics.height) * scale;
	void* drawFont = canvas.font;
	TextMetrics drawMetrics = metrics;
	float drawScale = scale;
	if (scale > 1.0f) {
		if (void* const replacement = g_combatFontCache.ResolveSharperArial(
			canvas.font, canvas.clipY, kMaximumCombatTextScale)) {
			const TextMetrics replacementMetrics =
				MeasureText(canvas, replacement, text.data);
			if (replacementMetrics.width > 0 && replacementMetrics.height > 0) {
				const float replacementScale = targetHeight /
					static_cast<float>(replacementMetrics.height);
				if (std::isfinite(replacementScale) && replacementScale > 0.0f) {
					drawFont = replacement;
					drawMetrics = replacementMetrics;
					drawScale = replacementScale;
				}
			}
		}
	}
	const float targetWidth =
		static_cast<float>(drawMetrics.width) * drawScale;
	const float shadowAdjustment =
		CombatShadowAdjustment(returnAddress, scale);
	canvas.curX += shadowAdjustment;
	if ((canvas.flags & 1u) == 0) {
		canvas.curX -=
			(targetWidth - static_cast<float>(metrics.width)) * 0.5f;
	}
	canvas.curY += shadowAdjustment -
		(targetHeight - static_cast<float>(metrics.height)) * 0.5f;

	int scaledWidth = 0;
	int scaledHeight = 0;
	g_drawTextCore(
		&canvas, 1, &scaledWidth, &scaledHeight, drawFont,
		drawScale, drawScale,
		(canvas.flags & 1u) != 0 ? 1 : 0, text.data);
#ifdef GA_CLIENT_DEBUG
	LogFontSelection(
		g_loggedCombatFontState, "combat popups", canvas.font,
		drawFont != canvas.font ? drawFont : nullptr,
		canvas.clipY, drawScale);
#endif
	RestoreCursor(canvas, retailResultCursor);
	return true;
}

void __fastcall DisplayDamageInfo(
	void* pawn, void* edx, CanvasLayout* canvas, float screenX,
	float screenY, std::uint32_t unused) {
	const CombatContext previous = g_combatContext;
	g_combatContext = {true, screenY};
	g_displayDamageInfo(pawn, edx, canvas, screenX, screenY, unused);
	g_combatContext = previous;
}

int __cdecl BorderedGlyphName(
	CanvasLayout* canvas, float x, float y, const StringLayout* text,
	void* font, std::uint32_t color, float retailScale) {
	const std::uintptr_t returnAddress =
		reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	const float scale = canvas ? FriendlyLabelScale(canvas->clipY) : 1.0f;
	if (!IsBorderedFriendlyLabelCall(returnAddress) ||
		std::fabs(scale - 1.0f) < 0.0001f ||
		!canvas || !text || !text->data || text->count <= 0 || !font ||
		!std::isfinite(retailScale) || retailScale <= 0.0f) {
		return g_borderedGlyphName(
			canvas, x, y, text, font, color, retailScale);
	}

	const TextMetrics metrics = MeasureText(*canvas, font, text->data);
	if (metrics.width <= 0 || metrics.height <= 0) {
		return g_borderedGlyphName(
			canvas, x, y, text, font, color, retailScale);
	}

	const float targetScale = retailScale * scale;
	const float widthDifference =
		static_cast<float>(metrics.width) * (targetScale - retailScale);
	const float heightDifference =
		static_cast<float>(metrics.height) * (targetScale - retailScale);
	return g_borderedGlyphName(
		canvas, x - widthDifference * 0.5f,
		y - heightDifference * 0.5f, text, font, color, targetScale);
}
}  // namespace

void ClientCombatTextScalePatch::Initialize() {
	int savedPercent = 0;
	const EngineConfig::LoadIntResult result = EngineConfig::LoadInt(
		kConfigSection, kConfigValue, &savedPercent);
	if (result == EngineConfig::LoadIntResult::Loaded) {
		g_scalePercent.store(
			CombatTextScaleSetting::Clamp(savedPercent),
			std::memory_order_relaxed);
	} else if (result == EngineConfig::LoadIntResult::Invalid) {
		Logger::Log(
			"clientpatch",
			"[combat-text-scale] invalid saved preference ignored\n");
	}
}

int ClientCombatTextScalePatch::ScalePercent() {
	return g_scalePercent.load(std::memory_order_relaxed);
}

bool ClientCombatTextScalePatch::ApplyScalePercent(int percent) {
	percent = CombatTextScaleSetting::Clamp(percent);
	g_scalePercent.store(percent, std::memory_order_relaxed);
	const bool saved =
		EngineConfig::SaveInt(kConfigSection, kConfigValue, percent);
	if (!saved) {
		Logger::Log(
			"clientpatch",
			"[combat-text-scale] preference save failed: "
			"engine config unavailable\n");
	}
#ifdef GA_CLIENT_DEBUG
	Logger::Log(
		"clientpatch",
		"[combat-text-scale] Apply: value=%d saved=%s\n",
		percent,
		saved ? "yes" : "no");
#endif
	return saved;
}

LONG ClientCombatTextScalePatch::Install() {
	LONG result = ClientCombatTextScalePatchBase::Install();
	if (result == NO_ERROR) {
		result = ::DetourAttach(
			reinterpret_cast<PVOID*>(&g_displayDamageInfo),
			reinterpret_cast<PVOID>(&DisplayDamageInfo));
	}
	if (result == NO_ERROR) {
		result = ::DetourAttach(
			reinterpret_cast<PVOID*>(&g_borderedGlyphName),
			reinterpret_cast<PVOID>(&BorderedGlyphName));
	}
	if (result == NO_ERROR) {
		result = ::DetourAttach(
			reinterpret_cast<PVOID*>(&g_rawGlyphName),
			reinterpret_cast<PVOID>(&RawGlyphName));
	}
	if (result == NO_ERROR) {
		result = ::DetourAttach(
			reinterpret_cast<PVOID*>(&g_drawMaterialTile),
			reinterpret_cast<PVOID>(&DrawMaterialTile));
	}
	if (result == NO_ERROR) {
		result = ::DetourAttach(
			reinterpret_cast<PVOID*>(&g_lowLevelRawText),
			reinterpret_cast<PVOID>(&LowLevelRawText));
	}
	if (result == NO_ERROR) {
		result = ::DetourAttach(
			reinterpret_cast<PVOID*>(&g_reticuleUpdate),
			reinterpret_cast<PVOID>(&ReticuleUpdate));
	}
	return result;
}

void __fastcall ClientCombatTextScalePatch::Call(
	void* canvasPointer, void* edx, const void* textPointer) {
	const std::uintptr_t returnAddress =
		reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	if (!canvasPointer || !textPointer) {
		CallOriginal(canvasPointer, edx, textPointer);
		return;
	}

	auto& canvas = *static_cast<CanvasLayout*>(canvasPointer);
	const auto& text = *static_cast<const StringLayout*>(textPointer);
	if (returnAddress == kRawGlyphNameDrawTextReturn &&
		DrawNormalizedRawName(canvas, text)) {
		return;
	}
	if (IsCombatTextCall(returnAddress) &&
		DrawScaledCombatText(canvas, text, returnAddress)) {
		return;
	}
	CallOriginal(canvasPointer, edx, textPointer);
}
