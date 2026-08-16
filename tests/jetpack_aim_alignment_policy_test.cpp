#include "src/ClientPatches/Camera/JetpackAimAlignment/JetpackAimAlignmentPolicy.hpp"

#include <cassert>
#include <cstdio>

using namespace ClientJetpackAimAlignment;

int main() {
	const Rotator invertedController{100, 200, 0x8000};
	const Rotator noShake{0, 0, 0};
	const Rotator corrected = CorrectedViewRotation(
		invertedController, noShake);
	assert(corrected.Pitch == 100);
	assert(corrected.Yaw == 200);
	assert(corrected.Roll == 0);

	const Rotator invertedShake{0, 0, -0x8000};
	assert(CorrectedViewRotation(noShake, invertedShake).Roll == 0);

	const Rotator wrappingController{0x7FF8, -0x7FF8, 1234};
	const Rotator wrappingShake{16, -16, 5678};
	const Rotator wrapped = CorrectedViewRotation(
		wrappingController, wrappingShake);
	assert(wrapped.Pitch == -0x7FF8);
	assert(wrapped.Yaw == 0x7FF8);
	assert(wrapped.Roll == 0);

	assert(!IsInversionRisk(0));
	assert(!IsInversionRisk(0x3FFF));
	assert(!IsInversionRisk(-0x3FFF));
	assert(IsInversionRisk(0x4000));
	assert(IsInversionRisk(-0x4000));
	assert(IsInversionRisk(0x8000));
	assert(!IsInversionRisk(0x10000));

	std::puts("jetpack aim alignment policy tests passed");
	return 0;
}
