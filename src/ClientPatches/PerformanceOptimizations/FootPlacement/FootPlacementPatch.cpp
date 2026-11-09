#include "src/ClientPatches/PerformanceOptimizations/FootPlacement/FootPlacementPatch.hpp"

#include "src/ClientRuntime/PatchSetting.hpp"
#include "src/Utils/HookBase.hpp"

namespace {

// thiscall on the pawn, no stack arguments, plain RET; its only caller (0x109E163D) treats EAX as a bool and
// enables each leg only when it is non-zero. The retail decision still runs so its side effects are kept.
class FootPlacementAllowedHook : public HookBase<
	int(__fastcall*)(void*, void*),
	0x109DA5B0u,
	FootPlacementAllowedHook> {
public:
	static int __fastcall Call(void* pawn, void* edx) {
		m_original(pawn, edx);
		return 0;
	}
};

}  // namespace

LONG ClientFootPlacementPatch::Install() {
	if (!PatchSetting::Enabled("FootPlacementOff", true)) return NO_ERROR;
	return FootPlacementAllowedHook::Install();
}
