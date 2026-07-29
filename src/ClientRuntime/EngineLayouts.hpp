#pragma once

#include <cstddef>
#include <cstdint>

// These views cover only fields touched by the client fixes and crash logger.
// They are valid for the reviewed 32-bit Global Agenda 1.5.1.5 executable.
static_assert(sizeof(void*) == 4, "The client patch must be compiled for 32-bit Global Agenda");

template <typename T>
struct TArray {
	// UE3's 32-bit dynamic-array header; allocation ownership remains with the
	// engine, and patches only read or compact elements in place.
	T* Data;
	int Count;
	int Max;
};

struct FNameEntry {
	// Only the verified prefix and inline name start are modeled. Entries are
	// reached by pointer, so longer engine names safely continue past this view.
	std::uint8_t unknown00[0x10];
	char Name[0x10];
};

// Absolute address in the supported non-ASLR retail executable.
inline constexpr std::uintptr_t GNames = 0x13454180u;

struct FName {
	int Index;
	std::uint32_t Number;

	const char* GetName() const {
		const auto* names = reinterpret_cast<const TArray<FNameEntry*>*>(GNames);
		if (!names->Data || Index < 0 || Index >= names->Count) return nullptr;
		const FNameEntry* entry = names->Data[Index];
		return entry ? entry->Name : nullptr;
	}
};

struct UClass;

struct UObject {
	// Common UObject prefix needed by ProcessEvent/FName dispatch only.
	std::uint8_t unknown00[0x28];
	UObject* Outer;
	FName Name;
	UClass* Class;
	UObject* ObjectArchetype;
};

struct UClass : UObject {};
struct UFunction : UObject {};

struct AActor {
	// A separate view avoids pretending that the unmodeled UObject/Actor body is
	// type-safe. Only Role is consumed by scoped-weapon ownership checks.
	std::uint8_t unknown00[0x28];
	UObject* Outer;
	FName Name;
	UClass* Class;
	UObject* ObjectArchetype;
	std::uint8_t unknown3C[0x92 - 0x3C];
	std::uint8_t Role;
};

struct UPrimitiveComponent {
	// Owner and the bHiddenGame flag group are the only component fields used.
	std::uint8_t unknown00[0x4C];
	AActor* Owner;
	std::uint8_t unknown50[0x118 - 0x50];
	std::uint32_t visibilityFlags;

	bool IsHiddenInGame() const {
		return (visibilityFlags & 0x4u) != 0;
	}
};

struct UTgDeviceForm;

struct ATgPawn {
	// Sparse TgPawn view for zoom state, equipped form index, and form pointers.
	std::uint8_t unknown00[0x3CC];
	std::uint32_t deviceVisibilityFlags;
	std::uint8_t unknown3D0[0x6EB - 0x3D0];
	std::uint8_t m_eEquippedInHand;
	std::uint8_t unknown6EC[0x890 - 0x6EC];
	UTgDeviceForm* c_EquipForm[25];

	bool IsDeviceHiddenDueToZoomVisual() const {
		return (deviceVisibilityFlags & 0x200u) != 0;
	}
};

struct UTgDeviceForm {
	// Sparse form view used to prove a component is one of the current in-hand
	// meshes before suppressing a visibility call.
	std::uint8_t unknown00[0x3C];
	ATgPawn* PawnOwner;
	std::uint8_t unknown40[0x78 - 0x40];
	UPrimitiveComponent* c_Mesh;
	UPrimitiveComponent* c_AttachedMesh;
};

struct FActiveMorph {
	// One UE3 active-morph record. Zero-weight records are removed only from the
	// transient render-update copy, never from the owning skeletal mesh state.
	void* Target;
	float Weight;
	std::uint32_t flags;
};

static_assert(sizeof(TArray<void*>) == 0x0C, "Unexpected TArray layout");
static_assert(sizeof(FName) == 0x08, "Unexpected FName layout");
static_assert(sizeof(UObject) == 0x3C, "Unexpected UObject layout");
static_assert(offsetof(UObject, Outer) == 0x28, "Unexpected UObject::Outer offset");
static_assert(offsetof(UObject, Name) == 0x2C, "Unexpected UObject::Name offset");
static_assert(offsetof(UObject, Class) == 0x34, "Unexpected UObject::Class offset");
static_assert(offsetof(AActor, Role) == 0x92, "Unexpected AActor::Role offset");
static_assert(offsetof(UPrimitiveComponent, Owner) == 0x4C,
	"Unexpected UPrimitiveComponent::Owner offset");
static_assert(offsetof(UPrimitiveComponent, visibilityFlags) == 0x118,
	"Unexpected UPrimitiveComponent visibility offset");
static_assert(offsetof(ATgPawn, deviceVisibilityFlags) == 0x3CC,
	"Unexpected ATgPawn device visibility offset");
static_assert(offsetof(ATgPawn, m_eEquippedInHand) == 0x6EB,
	"Unexpected ATgPawn equipped-slot offset");
static_assert(offsetof(ATgPawn, c_EquipForm) == 0x890,
	"Unexpected ATgPawn form-array offset");
static_assert(offsetof(UTgDeviceForm, PawnOwner) == 0x3C,
	"Unexpected UTgDeviceForm::PawnOwner offset");
static_assert(offsetof(UTgDeviceForm, c_Mesh) == 0x78,
	"Unexpected UTgDeviceForm::c_Mesh offset");
static_assert(offsetof(UTgDeviceForm, c_AttachedMesh) == 0x7C,
	"Unexpected UTgDeviceForm::c_AttachedMesh offset");
static_assert(sizeof(FActiveMorph) == 0x0C, "Unexpected FActiveMorph layout");
