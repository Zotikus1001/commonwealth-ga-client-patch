#include "src/ClientPatches/UI/F2StatsScaling/F2StatsScalingPatch.hpp"

#include "src/ClientRuntime/EngineFont.hpp"
#include "src/Utils/GameWindow/GameWindow.hpp"
#include "src/Utils/Logger/Logger.hpp"

#include <atomic>
#include <cmath>

namespace {

constexpr int kReferenceVerticalResolution = 1080;
constexpr std::uintptr_t kRawDrawTextAddress = 0x10ed4750u;
constexpr std::uintptr_t kF2StatsAddress = 0x10bca790u;
constexpr std::uintptr_t kF2TextAddress = 0x10ed5440u;
constexpr std::uintptr_t kF2FpsReturn = 0x10bca8c7u;
constexpr std::uintptr_t kF2PingReturn = 0x10bca9d9u;
constexpr std::uintptr_t kF2FrameTimeReturn = 0x10bcaa49u;
constexpr std::uintptr_t kTextShadowColorAddress = 0x119976b8u;
constexpr float kTransparentTextColor[4] = {};

struct F2Context {
	bool active;
	float scale;
	float baseY;
	float viewportHeight;
};

using RawDrawTextFunction = int(__cdecl*)(
	void*, float, float, const wchar_t*, void*, const void*, float, float,
	float, float*);
using F2StatsFunction = int(__cdecl*)(void*, void*, int, int);
using F2TextFunction = void(__cdecl*)(
	void*, float, float, const wchar_t*, void*, const void*);

RawDrawTextFunction g_rawDrawText =
	reinterpret_cast<RawDrawTextFunction>(kRawDrawTextAddress);
F2StatsFunction g_f2Stats =
	reinterpret_cast<F2StatsFunction>(kF2StatsAddress);
F2TextFunction g_f2Text =
	reinterpret_cast<F2TextFunction>(kF2TextAddress);
thread_local F2Context g_f2Context = {};
EngineFont::LoadedFontCache g_f2FontCache;

#ifdef GA_CLIENT_DEBUG
std::atomic<int> g_loggedVerticalResolution{-1};
std::atomic<int> g_loggedFontState{0};
#endif

int CurrentVerticalResolution() {
	return GameWindow::CurrentClientSize().height;
}

float ScaleForVerticalResolution(int verticalResolution) {
	if (verticalResolution <= 0) return 1.0f;
	// Every additional 1080 vertical pixels adds 50%, so 2160p uses 150%.
	return 1.0f +
		static_cast<float>(
			verticalResolution - kReferenceVerticalResolution) /
		static_cast<float>(kReferenceVerticalResolution * 2);
}

bool IsF2TextCall(std::uintptr_t returnAddress) {
	return returnAddress == kF2FpsReturn ||
		returnAddress == kF2PingReturn ||
		returnAddress == kF2FrameTimeReturn;
}

int __cdecl F2Stats(
	void* context, void* viewport, int x, int y) {
	const int verticalResolution = CurrentVerticalResolution();
	const float scale = ScaleForVerticalResolution(verticalResolution);
#ifdef GA_CLIENT_DEBUG
	if (g_loggedVerticalResolution.exchange(
			verticalResolution, std::memory_order_relaxed) !=
		verticalResolution) {
		Logger::Log(
			"clientpatch",
			"[f2-stats-scaling] vertical=%d scale=%.2f\n",
			verticalResolution,
			static_cast<double>(scale));
	}
#endif
	if (scale == 1.0f) return g_f2Stats(context, viewport, x, y);

	const F2Context previous = g_f2Context;
	g_f2Context = {
		true, scale, static_cast<float>(y),
		static_cast<float>(verticalResolution)};
	const int retailEndY = g_f2Stats(context, viewport, x, y);
	g_f2Context = previous;
	if (retailEndY < y) return retailEndY;
	return static_cast<int>(std::lround(
		static_cast<float>(y) +
		static_cast<float>(retailEndY - y) * scale));
}

void __cdecl F2Text(
	void* renderer, float x, float y, const wchar_t* text,
	void* font, const void* color) {
	const std::uintptr_t returnAddress =
		reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	if (!g_f2Context.active || !IsF2TextCall(returnAddress) ||
		g_f2Context.scale == 1.0f || !renderer || !text || !font) {
		g_f2Text(renderer, x, y, text, font, color);
		return;
	}

	const int retailWidth = g_rawDrawText(
		renderer, 0.0f, 0.0f, text, font, kTransparentTextColor,
		1.0f, 1.0f, 0.0f, nullptr);
	if (retailWidth <= 0) {
		g_f2Text(renderer, x, y, text, font, color);
		return;
	}
	void* drawFont = font;
	float drawScale = g_f2Context.scale;
	if (g_f2Context.scale > 1.0f) {
		if (void* const replacement = g_f2FontCache.ResolveSharperArial(
			font, g_f2Context.viewportHeight, g_f2Context.scale)) {
			const float compensated = EngineFont::CompensatedScale(
				font, replacement, g_f2Context.viewportHeight,
				g_f2Context.scale);
			if (std::isfinite(compensated) && compensated > 0.0f) {
				drawFont = replacement;
				drawScale = compensated;
			}
		}
	}
	int targetWidth = g_rawDrawText(
		renderer, 0.0f, 0.0f, text, drawFont, kTransparentTextColor,
		drawScale, drawScale, 0.0f, nullptr);
	if (targetWidth <= 0 && drawFont != font) {
		drawFont = font;
		drawScale = g_f2Context.scale;
		targetWidth = g_rawDrawText(
			renderer, 0.0f, 0.0f, text, drawFont, kTransparentTextColor,
			drawScale, drawScale, 0.0f, nullptr);
	}
	if (targetWidth <= 0) {
		g_f2Text(renderer, x, y, text, font, color);
		return;
	}

	// Retail right-aligns each row. Shift by the extra glyph width plus its
	// one-pixel shadow so scaling cannot clip against the window edge.
	const float shadowOffset = g_f2Context.scale;
	const float scaledX = x + static_cast<float>(retailWidth) + 1.0f -
		static_cast<float>(targetWidth) - shadowOffset;
	const float scaledY = g_f2Context.baseY +
		(y - g_f2Context.baseY) * g_f2Context.scale;
	g_rawDrawText(
		renderer, scaledX + shadowOffset, scaledY + shadowOffset, text,
		drawFont, reinterpret_cast<const void*>(kTextShadowColorAddress),
		drawScale, drawScale, 0.0f, nullptr);
	g_rawDrawText(
		renderer, scaledX, scaledY, text, drawFont, color,
		drawScale, drawScale, 0.0f, nullptr);
#ifdef GA_CLIENT_DEBUG
	const int fontState = drawFont != font ? 2 : 1;
	if (g_loggedFontState.exchange(
		fontState, std::memory_order_relaxed) != fontState) {
		Logger::Log(
			"clientpatch",
			"[scaled-text] F2 font: source=%s replacement=%s "
			"source-height=%.1f replacement-height=%.1f draw-scale=%.3f\n",
			EngineFont::Name(font) ? EngineFont::Name(font) : "unknown",
			drawFont != font && EngineFont::Name(drawFont)
				? EngineFont::Name(drawFont)
				: "retail",
			static_cast<double>(EngineFont::EffectiveHeight(
				font, g_f2Context.viewportHeight)),
			static_cast<double>(drawFont != font
				? EngineFont::EffectiveHeight(
					drawFont, g_f2Context.viewportHeight)
				: 0.0f),
			static_cast<double>(drawScale));
	}
#endif
}

}  // namespace

LONG ClientF2StatsScalingPatch::Install() {
	LONG result = ::DetourAttach(
		reinterpret_cast<PVOID*>(&g_f2Stats),
		reinterpret_cast<PVOID>(&F2Stats));
	if (result == NO_ERROR) {
		result = ::DetourAttach(
			reinterpret_cast<PVOID*>(&g_f2Text),
			reinterpret_cast<PVOID>(&F2Text));
	}
	return result;
}
