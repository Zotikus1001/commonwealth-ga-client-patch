#include "src/ClientPatches/Camera/JetpackAimAlignment/JetpackAimAlignmentPatch.hpp"

#ifdef GA_CLIENT_DEBUG
#include <atomic>

#include "src/Utils/Logger/Logger.hpp"
#endif

struct JetpackAimVector {
	float X;
	float Y;
	float Z;
};

using ClientJetpackAimAlignment::CorrectedViewRotation;
using ClientJetpackAimAlignment::IsInversionRisk;
using ClientJetpackAimAlignment::Rotator;

struct JetpackAimControllerLayout {
	std::uint8_t unknown00[0x60];
	Rotator Rotation;
	std::uint8_t unknown6C[0x388 - 0x6C];
	void* Pawn;
	std::uint8_t unknown38C[0x720 - 0x38C];
	Rotator ShakeRotation;
};

static_assert(sizeof(JetpackAimVector) == 0x0C,
	"Unexpected FVector layout");
static_assert(sizeof(Rotator) == 0x0C,
	"Unexpected FRotator layout");
static_assert(offsetof(JetpackAimControllerLayout, Rotation) == 0x60,
	"Unexpected AActor::Rotation offset");
static_assert(offsetof(JetpackAimControllerLayout, Pawn) == 0x388,
	"Unexpected AController::Pawn offset");
static_assert(offsetof(JetpackAimControllerLayout, ShakeRotation) == 0x720,
	"Unexpected combined view-shake rotation offset");

namespace {

constexpr std::uintptr_t kGetStateNameAddress = 0x109B22F0u;
constexpr char kPlayerJettingState[] = "PlayerJetting";

using GetStateNameFunction = FName* (__cdecl*)(
	FName*, const JetpackAimControllerLayout*);

bool IsPlayerJetting(const JetpackAimControllerLayout* controller) {
	if (!controller || !controller->Pawn) return false;

	FName state{};
	const auto getStateName =
		reinterpret_cast<GetStateNameFunction>(kGetStateNameAddress);
	getStateName(&state, controller);
	const char* const name = state.GetName();
	return name && std::strcmp(name, kPlayerJettingState) == 0;
}

#ifdef GA_CLIENT_DEBUG
std::atomic<bool> g_rollGuardActive{false};

void LogRollGuard(
	const Rotator& controllerRotation,
	const Rotator& shakeRotation,
	const Rotator& retailRotation) {
	const bool inversionRisk = IsInversionRisk(controllerRotation.Roll) ||
		IsInversionRisk(shakeRotation.Roll) ||
		IsInversionRisk(retailRotation.Roll);
	if (g_rollGuardActive.exchange(
			inversionRisk, std::memory_order_relaxed) == inversionRisk) {
		return;
	}
	if (inversionRisk) {
		Logger::Log(
			"clientpatch",
			"[jetpack-aim] blocked inversion-class roll: "
			"controller=%d shake=%d retail=%d corrected=0\n",
			controllerRotation.Roll,
			shakeRotation.Roll,
			retailRotation.Roll);
	} else {
		Logger::Log(
			"clientpatch",
			"[jetpack-aim] inversion-class roll cleared\n");
	}
}

void ClearRollGuardLogState() {
	if (!g_rollGuardActive.exchange(false, std::memory_order_relaxed)) return;
	Logger::Log(
		"clientpatch",
		"[jetpack-aim] inversion-class roll cleared after PlayerJetting\n");
}
#endif

}  // namespace

void __fastcall ClientJetpackAimAlignmentPatch::Call(
	JetpackAimControllerLayout* controller,
	void* edx,
	JetpackAimVector* outLocation,
	Rotator* outRotation) {
	const bool alignAim = IsPlayerJetting(controller);
	m_original(controller, edx, outLocation, outRotation);

#ifdef GA_CLIENT_DEBUG
	if (!alignAim) ClearRollGuardLogState();
#endif
	if (!alignAim || !outRotation) return;

	// PlayerJetting can return stale script-owned camera offsets even while
	// Controller.Rotation tracks visible pitch and yaw. Replace only the
	// rotation consumed by pawn aim; preserve retail camera position.
#ifdef GA_CLIENT_DEBUG
	const Rotator retailRotation = *outRotation;
#endif
	*outRotation = CorrectedViewRotation(
		controller->Rotation, controller->ShakeRotation);
#ifdef GA_CLIENT_DEBUG
	LogRollGuard(
		controller->Rotation, controller->ShakeRotation, retailRotation);
#endif
}
