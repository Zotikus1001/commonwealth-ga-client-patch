#pragma once

// GLOBAL CLIENT PATCH: Scoped Weapon Visibility
// Applies to LIVE and private servers in every client DLL build.
//
// TgPawn's mesh-visibility helper at 0x10a59400 propagates body visibility to every skeletal attachment.
// TgDeviceForm::SetVisibility at 0x10a98230 separately owns the current in-hand meshes. While scoped, the body
// helper shows the sniper at return site 0x10a59523 and the device helper hides it again every tick, forcing a
// render-object reattach each time. Ignore only body-helper attempts to show the local pawn's current form meshes
// while c_bDeviceHiddenDueToZoomVisual still requires them hidden. Every other visibility path stays original.

#include "src/pch.hpp"
#include "src/Utils/HookBase.hpp"

class ClientScopedWeaponVisibilityPatch : public HookBase<
	void(__fastcall*)(UPrimitiveComponent* /*component=ECX*/, void* /*edx*/, int /*newHidden*/),
	0x10c91f30, ClientScopedWeaponVisibilityPatch> {
public:
	static void __fastcall Call(UPrimitiveComponent* component, void* edx, int newHidden);
	static inline void __fastcall CallOriginal(UPrimitiveComponent* component, void* edx, int newHidden) {
		m_original(component, edx, newHidden);
	}
};
