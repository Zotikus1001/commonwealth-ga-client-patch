#include "src/ClientPatches/Camera/JetpackAimAlignment/JetpackAimAlignmentPatch.hpp"

struct JetpackAimVector {
	float X;
	float Y;
	float Z;
};

struct JetpackAimRotator {
	std::int32_t Pitch;
	std::int32_t Yaw;
	std::int32_t Roll;
};

struct JetpackAimControllerLayout {
	std::uint8_t unknown00[0x60];
	JetpackAimRotator Rotation;
	std::uint8_t unknown6C[0x388 - 0x6C];
	void* Pawn;
	std::uint8_t unknown38C[0x720 - 0x38C];
	JetpackAimRotator ShakeRotation;
};

static_assert(sizeof(JetpackAimVector) == 0x0C,
	"Unexpected FVector layout");
static_assert(sizeof(JetpackAimRotator) == 0x0C,
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

std::int32_t AddRotatorComponents(std::int32_t lhs, std::int32_t rhs) {
	const std::uint32_t wrapped =
		(static_cast<std::uint32_t>(lhs) +
		 static_cast<std::uint32_t>(rhs)) & 0xFFFFu;
	return wrapped > 0x7FFFu
		? static_cast<std::int32_t>(wrapped) - 0x10000
		: static_cast<std::int32_t>(wrapped);
}

}  // namespace

void __fastcall ClientJetpackAimAlignmentPatch::Call(
	JetpackAimControllerLayout* controller,
	void* edx,
	JetpackAimVector* outLocation,
	JetpackAimRotator* outRotation) {
	const bool alignAim = IsPlayerJetting(controller);
	m_original(controller, edx, outLocation, outRotation);

	if (!alignAim || !outRotation) return;

	// PlayerJetting can return stale script-owned camera offsets even while
	// Controller.Rotation tracks the visible view. Replace only the rotation
	// consumed by pawn aim; preserve retail camera position and shake.
	outRotation->Pitch = AddRotatorComponents(
		controller->Rotation.Pitch, controller->ShakeRotation.Pitch);
	outRotation->Yaw = AddRotatorComponents(
		controller->Rotation.Yaw, controller->ShakeRotation.Yaw);
	outRotation->Roll = AddRotatorComponents(
		controller->Rotation.Roll, controller->ShakeRotation.Roll);
}
