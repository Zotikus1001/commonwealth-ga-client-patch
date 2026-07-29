#include "src/ClientPatches/MorphRebuildPerformance/MorphRebuildPerformancePatch.hpp"

#ifdef GA_CLIENT_DEBUG
#include "src/Utils/Logger/Logger.hpp"
#endif

#include <cmath>

namespace {

constexpr float kEngineWeightThreshold = 0.001f;

struct DynamicDataView {
	unsigned char unknown00[0x1c];
	int lodIndex;
	TArray<FActiveMorph> activeMorphs;
	int numWeightedActiveMorphs;
};

static_assert(offsetof(DynamicDataView, lodIndex) == 0x1c, "Unexpected LODIndex offset");
static_assert(offsetof(DynamicDataView, activeMorphs) == 0x20, "Unexpected ActiveMorphs offset");
static_assert(offsetof(DynamicDataView, numWeightedActiveMorphs) == 0x2c,
	"Unexpected NumWeightedActiveMorphs offset");

int CompactContributingMorphs(DynamicDataView* data) {
	if (!data || data->activeMorphs.Count <= 0 || !data->activeMorphs.Data) {
		if (data) data->numWeightedActiveMorphs = 0;
		return 0;
	}

	const int originalCount = data->activeMorphs.Count;
	int writeIndex = 0;
	for (int readIndex = 0; readIndex < originalCount; ++readIndex) {
		const FActiveMorph& morph = data->activeMorphs.Data[readIndex];
		if (std::fabs(morph.Weight) < kEngineWeightThreshold) continue;
		if (writeIndex != readIndex) data->activeMorphs.Data[writeIndex] = morph;
		++writeIndex;
	}

	data->activeMorphs.Count = writeIndex;
	data->numWeightedActiveMorphs = writeIndex;
	return originalCount - writeIndex;
}

}  // namespace

void __fastcall ClientMorphRebuildPerformancePatch::Call(
	void* meshObject, void* edx, void* incomingDynamicData) {
	auto* incoming = static_cast<DynamicDataView*>(incomingDynamicData);
	const int originalCount = incoming ? incoming->activeMorphs.Count : 0;
	const int removed = CompactContributingMorphs(incoming);
	const int compactedCount = incoming ? incoming->activeMorphs.Count : 0;

#ifdef GA_CLIENT_DEBUG
	LARGE_INTEGER start{};
	LARGE_INTEGER end{};
	QueryPerformanceCounter(&start);
#endif

	CallOriginal(meshObject, edx, incomingDynamicData);

#ifdef GA_CLIENT_DEBUG
	QueryPerformanceCounter(&end);
	if (removed > 0 && Logger::IsChannelEnabled("morphprofile")) {
		static LARGE_INTEGER frequency = [] {
			LARGE_INTEGER value{};
			QueryPerformanceFrequency(&value);
			return value;
		}();
		const double elapsed = frequency.QuadPart > 0
			? static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
				static_cast<double>(frequency.QuadPart)
			: 0.0;
		Logger::Log("morphprofile", "[compact] active=%d -> %d removed=%d elapsed=%.3f ms\n",
			originalCount, compactedCount, removed, elapsed);
	}
#else
	(void)originalCount;
	(void)removed;
	(void)compactedCount;
#endif
}
