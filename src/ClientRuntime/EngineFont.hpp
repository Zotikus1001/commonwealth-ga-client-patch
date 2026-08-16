#pragma once

#include "src/ClientRuntime/EngineFontCachePolicy.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace EngineFont {

class LoadedFontCache {
public:
	explicit LoadedFontCache(const char* diagnosticName);

	void* ResolveSharperArial(
		void* sourceFont, float viewportHeight, float maximumVisualScale,
		bool preferBold = false);

private:
	struct Slot {
		std::atomic<void*> font{nullptr};
		std::atomic<void*> sourceFont{nullptr};
		std::atomic<int> objectIndex{-1};
		std::atomic<bool> bold{false};
	};

	std::array<Slot, EngineFontCachePolicy::kEntryCapacity> entries_{};
	std::atomic<std::uint32_t> nextScanTick_{0};
	std::atomic<std::uint32_t> nextVictim_{0};
#ifdef GA_CLIENT_DEBUG
	const char* diagnosticName_;
#endif
};

const char* Name(const void* font);
float EffectiveHeight(void* font, float viewportHeight);
float EffectiveTextHeight(
	void* font, float viewportHeight, const wchar_t* text);
float CompensatedScale(
	void* sourceFont, void* replacementFont, float viewportHeight,
	float sourceDrawScale);
float CompensatedTextScale(
	void* sourceFont, void* replacementFont, float viewportHeight,
	const wchar_t* text, float sourceDrawScale);

}  // namespace EngineFont
