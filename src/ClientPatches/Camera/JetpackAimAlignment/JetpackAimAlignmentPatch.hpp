#pragma once

#include "src/ClientPatches/Camera/JetpackAimAlignment/JetpackAimAlignmentPolicy.hpp"
#include "src/pch.hpp"
#include "src/Utils/HookBase.hpp"

struct JetpackAimControllerLayout;
struct JetpackAimVector;

class ClientJetpackAimAlignmentPatch : public HookBase<
	void(__fastcall*)(
		JetpackAimControllerLayout*,
		void*,
		JetpackAimVector*,
		ClientJetpackAimAlignment::Rotator*),
	0x109692C0u,
	ClientJetpackAimAlignmentPatch> {
public:
	static void __fastcall Call(
		JetpackAimControllerLayout* controller,
		void* edx,
		JetpackAimVector* outLocation,
		ClientJetpackAimAlignment::Rotator* outRotation);
};
