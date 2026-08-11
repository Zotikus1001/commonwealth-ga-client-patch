#include "src/ClientRuntime/EngineFont.hpp"

#include "src/pch.hpp"

#include <cmath>
#include <cstring>

namespace {

constexpr std::uintptr_t kGlobalObjectsAddress = 0x13465A54u;
constexpr int kMaximumObjectCount = 1000000;
constexpr std::uint32_t kLookupRetryMilliseconds = 5000;
constexpr std::size_t kFontCharacterOffsetVtableOffset = 0x118u;
constexpr std::size_t kFontResolutionScaleVtableOffset = 0x11cu;
constexpr std::size_t kFontMaximumHeightVtableOffset = 0x124u;

constexpr const char* kRegularArialCandidates[] = {
	"GA_Font_ArialReg_30pt",
	"GA_Font_ArialReg_24pt",
	"GA_Font_Arial_22pt",
	"GA_Font_Arial_Reg_20pt",
	"GA_Font_Arial_Reg_18pt",
	"GA_Font_ArialReg_16pt",
};

constexpr const char* kBoldArialCandidates[] = {
	"GA_Font_ArialBold_36pt",
	"GA_Font_ArialBold_30pt",
	"GA_Font_ArialBold_24pt",
	"GA_Font_ArialBold_22pt",
	"GA_Font_ArialBold_20pt",
	"GA_Font_ArialBold_18pt",
	"GA_Font_ArialBold_16pt",
};

struct ClassLayout {
	UObject Object;
	ClassLayout* SuperField;
};

static_assert(offsetof(ClassLayout, SuperField) == 0x3c,
	"Unexpected UClass::SuperField offset");

struct FontCharacterLayout {
	int startU;
	int startV;
	int width;
	int height;
	std::uint8_t textureIndex;
	std::uint8_t unknown11[3];
	int verticalOffset;
};

struct FontLayout {
	std::uint8_t unknown00[0x3c];
	TArray<FontCharacterLayout> characters;
	std::uint8_t unknown48[0x48];
	std::uint32_t isRemapped;
};

static_assert(sizeof(FontCharacterLayout) == 0x18,
	"Unexpected UFont character size");
static_assert(offsetof(FontCharacterLayout, height) == 0x0c,
	"Unexpected UFont character height offset");
static_assert(offsetof(FontLayout, characters) == 0x3c,
	"Unexpected UFont character-array offset");
static_assert(offsetof(FontLayout, isRemapped) == 0x90,
	"Unexpected UFont remap flag offset");

using FontCharacterOffsetFunction = int(__thiscall*)(void*, float);
using FontResolutionScaleFunction =
	float(__thiscall*)(void*, float);
using FontMaximumHeightFunction = float(__thiscall*)(void*);

const TArray<UObject*>* GlobalObjects() {
	const auto* objects =
		reinterpret_cast<const TArray<UObject*>*>(kGlobalObjectsAddress);
	if (!objects->Data || objects->Count < 0 ||
		objects->Count > kMaximumObjectCount || objects->Max < objects->Count) {
		return nullptr;
	}
	return objects;
}

bool ObjectNameEquals(const UObject* object, const char* expected) {
	if (!object || !expected) return false;
	const char* const name = object->Name.GetName();
	return name && std::strcmp(name, expected) == 0;
}

bool IsFont(const UObject* object) {
	if (!object || !object->Class) return false;
	const auto* current =
		reinterpret_cast<const ClassLayout*>(object->Class);
	for (int depth = 0; current && depth < 64; ++depth) {
		if (ObjectNameEquals(&current->Object, "Font")) return true;
		current = current->SuperField;
	}
	return false;
}

bool NameInCandidates(
	const UObject* object, const char* const* candidates,
	std::size_t candidateCount) {
	const char* const name = EngineFont::Name(object);
	if (!name) return false;
	for (std::size_t index = 0; index < candidateCount; ++index) {
		if (std::strcmp(name, candidates[index]) == 0) return true;
	}
	return false;
}

bool IsBold(const void* font) {
	const char* const name = EngineFont::Name(font);
	return name && std::strstr(name, "Bold") != nullptr;
}

bool TickReached(std::uint32_t now, std::uint32_t target) {
	return static_cast<std::int32_t>(now - target) >= 0;
}

void* FindBestLoadedFont(
	const char* const* candidates, std::size_t candidateCount,
	float viewportHeight, float sourceHeight, float targetHeight) {
	const TArray<UObject*>* const objects = GlobalObjects();
	if (!objects) return nullptr;

	void* smallestSufficient = nullptr;
	float smallestSufficientHeight = 0.0f;
	void* largestImprovement = nullptr;
	float largestImprovementHeight = sourceHeight;
	for (int index = 0; index < objects->Count; ++index) {
		UObject* const object = objects->Data[index];
		if (!NameInCandidates(object, candidates, candidateCount) ||
			!IsFont(object)) {
			continue;
		}

		const float height =
			EngineFont::EffectiveHeight(object, viewportHeight);
		if (!std::isfinite(height) || height <= sourceHeight) continue;
		if (height > largestImprovementHeight) {
			largestImprovement = object;
			largestImprovementHeight = height;
		}
		if (height >= targetHeight &&
			(!smallestSufficient || height < smallestSufficientHeight)) {
			smallestSufficient = object;
			smallestSufficientHeight = height;
		}
	}
	return smallestSufficient ? smallestSufficient : largestImprovement;
}

}  // namespace

void* EngineFont::LoadedFontCache::ResolveSharperArial(
	void* sourceFont, float viewportHeight, float maximumVisualScale,
	bool preferBold) {
	if (!sourceFont || !std::isfinite(viewportHeight) || viewportHeight <= 0.0f ||
		!std::isfinite(maximumVisualScale) || maximumVisualScale <= 1.0f) {
		return nullptr;
	}
	if (void* const cached = font_.load(std::memory_order_acquire)) {
		return cached;
	}

	const std::uint32_t now = ::GetTickCount();
	std::uint32_t next = nextLookupTick_.load(std::memory_order_relaxed);
	if (!TickReached(now, next) ||
		!nextLookupTick_.compare_exchange_strong(
			next, now + kLookupRetryMilliseconds,
			std::memory_order_acq_rel, std::memory_order_relaxed)) {
		return nullptr;
	}

	const float sourceHeight = EffectiveHeight(sourceFont, viewportHeight);
	if (!std::isfinite(sourceHeight) || sourceHeight <= 0.0f) return nullptr;
	const float targetHeight = sourceHeight * maximumVisualScale;
	const bool sourceIsBold = IsBold(sourceFont);
	const bool bold = preferBold || sourceIsBold;
	const char* const* candidates = bold
		? kBoldArialCandidates
		: kRegularArialCandidates;
	const std::size_t candidateCount = bold
		? sizeof(kBoldArialCandidates) / sizeof(kBoldArialCandidates[0])
		: sizeof(kRegularArialCandidates) / sizeof(kRegularArialCandidates[0]);
	void* replacement = FindBestLoadedFont(
		candidates, candidateCount, viewportHeight, sourceHeight, targetHeight);
	if (!replacement && preferBold && !sourceIsBold) {
		replacement = FindBestLoadedFont(
			kRegularArialCandidates,
			sizeof(kRegularArialCandidates) /
				sizeof(kRegularArialCandidates[0]),
			viewportHeight, sourceHeight, targetHeight);
	}
	if (replacement) {
		font_.store(replacement, std::memory_order_release);
	}
	return replacement;
}

const char* EngineFont::Name(const void* font) {
	if (!font) return nullptr;
	return static_cast<const UObject*>(font)->Name.GetName();
}

float EngineFont::EffectiveHeight(void* font, float viewportHeight) {
	if (!font || !std::isfinite(viewportHeight) || viewportHeight <= 0.0f) {
		return 0.0f;
	}
	void** const vtable = *static_cast<void***>(font);
	if (!vtable) return 0.0f;
	const auto resolutionScale = reinterpret_cast<FontResolutionScaleFunction>(
		vtable[kFontResolutionScaleVtableOffset / sizeof(void*)]);
	const auto maximumHeight = reinterpret_cast<FontMaximumHeightFunction>(
		vtable[kFontMaximumHeightVtableOffset / sizeof(void*)]);
	if (!resolutionScale || !maximumHeight) return 0.0f;
	const float scale = resolutionScale(font, viewportHeight);
	const float height = maximumHeight(font);
	if (!std::isfinite(scale) || scale <= 0.0f ||
		!std::isfinite(height) || height <= 0.0f) {
		return 0.0f;
	}
	return height * scale;
}

float EngineFont::EffectiveTextHeight(
	void* font, float viewportHeight, const wchar_t* text) {
	if (!font || !text || !*text || !std::isfinite(viewportHeight) ||
		viewportHeight <= 0.0f) {
		return 0.0f;
	}
	const auto* const layout = static_cast<const FontLayout*>(font);
	if (layout->isRemapped != 0 || !layout->characters.Data ||
		layout->characters.Count < 0 ||
		layout->characters.Count > kMaximumObjectCount ||
		layout->characters.Max < layout->characters.Count) {
		return 0.0f;
	}

	void** const vtable = *static_cast<void***>(font);
	if (!vtable) return 0.0f;
	const auto characterOffset =
		reinterpret_cast<FontCharacterOffsetFunction>(
			vtable[kFontCharacterOffsetVtableOffset / sizeof(void*)]);
	const auto resolutionScale =
		reinterpret_cast<FontResolutionScaleFunction>(
			vtable[kFontResolutionScaleVtableOffset / sizeof(void*)]);
	if (!characterOffset || !resolutionScale) return 0.0f;

	const int offset = characterOffset(font, viewportHeight);
	int maximumHeight = 0;
	for (const wchar_t* cursor = text; *cursor; ++cursor) {
		const std::int64_t index = static_cast<std::int64_t>(offset) +
			static_cast<std::uint16_t>(*cursor);
		if (index < 0 || index >= layout->characters.Count) continue;
		const int height = layout->characters.Data[index].height;
		if (height > maximumHeight) maximumHeight = height;
	}
	const float scale = resolutionScale(font, viewportHeight);
	if (maximumHeight <= 0 || !std::isfinite(scale) || scale <= 0.0f) {
		return 0.0f;
	}
	return static_cast<float>(maximumHeight) * scale;
}

float EngineFont::CompensatedScale(
	void* sourceFont, void* replacementFont, float viewportHeight,
	float sourceDrawScale) {
	if (!std::isfinite(sourceDrawScale) || sourceDrawScale <= 0.0f) {
		return sourceDrawScale;
	}
	const float sourceHeight = EffectiveHeight(sourceFont, viewportHeight);
	const float replacementHeight =
		EffectiveHeight(replacementFont, viewportHeight);
	if (sourceHeight <= 0.0f || replacementHeight <= 0.0f) {
		return sourceDrawScale;
	}
	return sourceDrawScale * sourceHeight / replacementHeight;
}

float EngineFont::CompensatedTextScale(
	void* sourceFont, void* replacementFont, float viewportHeight,
	const wchar_t* text, float sourceDrawScale) {
	if (!std::isfinite(sourceDrawScale) || sourceDrawScale <= 0.0f) {
		return sourceDrawScale;
	}
	const float sourceHeight =
		EffectiveTextHeight(sourceFont, viewportHeight, text);
	const float replacementHeight =
		EffectiveTextHeight(replacementFont, viewportHeight, text);
	if (sourceHeight <= 0.0f || replacementHeight <= 0.0f) {
		return CompensatedScale(
			sourceFont, replacementFont, viewportHeight, sourceDrawScale);
	}
	return sourceDrawScale * sourceHeight / replacementHeight;
}
