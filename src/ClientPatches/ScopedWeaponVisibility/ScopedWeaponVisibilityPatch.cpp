#include "src/ClientPatches/ScopedWeaponVisibility/ScopedWeaponVisibilityPatch.hpp"

#ifdef GA_CLIENT_DEBUG
#include "src/Utils/Logger/Logger.hpp"
#endif

namespace {

constexpr uintptr_t kPawnAttachmentSetHiddenReturn = 0x10a59523;
constexpr unsigned char kRoleAutonomousProxy = 2;
constexpr unsigned char kEquipSlotCount = 25;

ATgPawn* LocalCharacterOwner(UPrimitiveComponent* component) {
	AActor* owner = component ? component->Owner : nullptr;
	if (!owner || owner->Role != kRoleAutonomousProxy || !owner->Class) return nullptr;
	const char* className = owner->Class->Name.GetName();
	if (!className || !strstr(className, "TgPawn_Character")) return nullptr;
	return reinterpret_cast<ATgPawn*>(owner);
}

UTgDeviceForm* CurrentInHandForm(ATgPawn* pawn) {
	if (!pawn || pawn->m_eEquippedInHand >= kEquipSlotCount) return nullptr;
	UTgDeviceForm* form = pawn->c_EquipForm[pawn->m_eEquippedInHand];
	return form && form->PawnOwner == pawn ? form : nullptr;
}

bool IsCurrentInHandMesh(UPrimitiveComponent* component, UTgDeviceForm* form) {
	return component && form && (component == form->c_Mesh || component == form->c_AttachedMesh);
}

}  // namespace

void __fastcall ClientScopedWeaponVisibilityPatch::Call(
	UPrimitiveComponent* component, void* edx, int newHidden) {
	ATgPawn* pawn = LocalCharacterOwner(component);
	UTgDeviceForm* form = CurrentInHandForm(pawn);
	const bool currentInHandMesh = IsCurrentInHandMesh(component, form);
	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(__builtin_return_address(0));

#ifdef GA_CLIENT_DEBUG
	static unsigned int suppressedBodyCalls = 0;
#endif

	const bool bodyTryingToOverrideZoom = currentInHandMesh &&
		returnAddress == kPawnAttachmentSetHiddenReturn && newHidden == 0 &&
		pawn->IsDeviceHiddenDueToZoomVisual();
	if (bodyTryingToOverrideZoom) {
#ifdef GA_CLIENT_DEBUG
		++suppressedBodyCalls;
#endif
		return;
	}

#ifdef GA_CLIENT_DEBUG
	const bool wasHidden = component && component->IsHiddenInGame();
#endif

	CallOriginal(component, edx, newHidden);

#ifdef GA_CLIENT_DEBUG
	if (currentInHandMesh && wasHidden != (newHidden != 0) &&
		Logger::IsChannelEnabled("morphprofile")) {
		Logger::Log("morphprofile",
			"[inhand-visibility] hidden=%d -> %d caller=0x%p suppressed-body-calls=%u\n",
			wasHidden ? 1 : 0, newHidden ? 1 : 0,
			reinterpret_cast<void*>(returnAddress), suppressedBodyCalls);
		suppressedBodyCalls = 0;
	}
#endif
}
