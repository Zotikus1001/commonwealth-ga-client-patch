#pragma once

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
	std::atomic<void*> font_{nullptr};
	std::atomic<void*> sourceFont_{nullptr};
	std::atomic<int> objectIndex_{-1};
	std::atomic<bool> bold_{false};
	std::atomic<std::uint32_t> nextLookupTick_{0};
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
