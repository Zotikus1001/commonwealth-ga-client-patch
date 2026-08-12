#pragma once

#include "src/pch.hpp"
#include "src/Utils/HookBase.hpp"

struct JetpackAimControllerLayout;
struct JetpackAimVector;
struct JetpackAimRotator;

class ClientJetpackAimAlignmentPatch : public HookBase<
	void(__fastcall*)(
		JetpackAimControllerLayout*,
		void*,
		JetpackAimVector*,
		JetpackAimRotator*),
	0x109692C0u,
	ClientJetpackAimAlignmentPatch> {
public:
	static void __fastcall Call(
		JetpackAimControllerLayout* controller,
		void* edx,
		JetpackAimVector* outLocation,
		JetpackAimRotator* outRotation);
};
