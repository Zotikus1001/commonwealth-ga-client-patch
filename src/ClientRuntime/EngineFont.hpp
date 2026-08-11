#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace EngineFont {

class LoadedFontCache {
public:
	void* ResolveSharperArial(
		void* sourceFont, float viewportHeight, float maximumVisualScale,
		bool preferBold = false);

private:
	std::atomic<void*> font_{nullptr};
	std::atomic<std::uint32_t> nextLookupTick_{0};
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
