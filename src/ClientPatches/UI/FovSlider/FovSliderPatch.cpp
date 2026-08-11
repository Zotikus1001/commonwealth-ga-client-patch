#include "src/ClientPatches/UI/FovSlider/FovSliderPatch.hpp"

#include "src/ClientPatches/UI/CombatTextScale/CombatTextScalePatch.hpp"
#include "src/ClientPatches/UI/CombatTextScale/CombatTextScaleSetting.hpp"
#include "src/ClientPatches/UI/FovSlider/FovSetting.hpp"
#include "src/ClientRuntime/EngineConfig.hpp"
#include "src/Utils/HookBase.hpp"
#include "src/Utils/Logger/Logger.hpp"

#include <cmath>
#include <cstring>
#include <cwchar>

namespace {

constexpr std::uintptr_t kGlobalObjectsAddress = 0x13465A54u;
constexpr std::uintptr_t kProcessEventAddress = 0x11347C20u;
constexpr std::uintptr_t kCreateWidgetAddress = 0x10B7E800u;
constexpr std::uintptr_t kGetPlayerOwnerAddress = 0x10B5E360u;
constexpr std::uintptr_t kSliderGetValueAddress = 0x10B5AF00u;
constexpr std::uintptr_t kSliderSetValueAddress = 0x10B5CC90u;
constexpr std::uintptr_t kDestroyStringAddress = 0x112C18C0u;
constexpr std::size_t kInsertChildVtableOffset = 0x128u;
constexpr std::uint32_t kHiddenFlag = 0x1u;
constexpr int kMaximumObjectCount = 1000000;
constexpr int kMaximumChildrenPerWidget = 4096;
// The retail right column ends this many Brightness widths past its left edge.
constexpr float kRightColumnWidthFraction = 1.67f;

constexpr wchar_t kConfigSection[] = L"Commonwealth.ClientPatch";
constexpr wchar_t kFovConfigValue[] = L"FieldOfView";
constexpr wchar_t kFovLabelFormat[] = L"FOV: %d";
constexpr wchar_t kCombatTextScalingLabelFormat[] =
	L"Combat Text Scaling: %d%%";

struct ClassLayout {
	UObject Object;
	ClassLayout* SuperField;
};

struct BoundsLayout {
	float Value[4];
	std::uint8_t ScaleType[4];
	std::uint8_t Invalidated[4];
	std::uint8_t AspectRatioMode;
	std::uint8_t padding19[3];
};

struct UiObjectLayout;

struct SliderRowGeometry {
	float brightnessLeft;
	float fovLeft;
	float combatTextLeft;
	float controlWidth;
	float rowWidth;
};

struct ScreenObjectLayout {
	UObject Object;
	BoundsLayout Position;
	float PositionOffset[2];
	void* ScrollFrameParent;
	float ZDepth;
	std::uint32_t Flags;
	TArray<UiObjectLayout*> Children;
};

struct ScriptDelegateLayout {
	UObject* Object;
	FName FunctionName;
};

struct UiObjectLayout {
	ScreenObjectLayout Screen;
	std::uint8_t unknown78[0x1C4 - 0x78];
	UiObjectLayout* Owner;
	std::uint8_t unknown1C8[0x39C - 0x1C8];
	ScriptDelegateLayout OnValueChanged;
};

struct RangeLayout {
	float CurrentValue;
	float MinimumValue;
	float MaximumValue;
	float NudgeValue;
	std::uint32_t Flags;
};

struct SliderLayout {
	UiObjectLayout Object;
	std::uint8_t unknown3A8[0x464 - 0x3A8];
	RangeLayout SliderValue;
};

struct VideoSceneLayout {
	UObject Object;
	std::uint8_t unknown3C[0x1F0 - 0x3C];
	UiObjectLayout* ApplyButton;
	UiObjectLayout* ResetButton;
	std::uint8_t unknown1F8[0x244 - 0x1F8];
	UiObjectLayout* ComboBoxes[8];
	std::uint8_t unknown264[0x42C - 0x264];
	SliderLayout* GammaCorrectionSlider;
};

struct PlayerControllerLayout {
	UObject Object;
	std::uint8_t unknown3C[0x1CC - 0x3C];
	UObject* Pawn;
};

struct LocalPlayerLayout {
	UObject Object;
	std::uint8_t unknown3C[0x40 - 0x3C];
	PlayerControllerLayout* Actor;
};

struct InputEventParametersLayout {
	UiObjectLayout* UiObjectReference;
};

struct StringLayout {
	wchar_t* Data;
	int Count;
	int Max;
};

struct ConsoleCommandParametersLayout {
	StringLayout Command;
	std::uint32_t WriteToLog;
	StringLayout ReturnValue;
};

static_assert(sizeof(ClassLayout) == 0x40, "Unexpected UClass prefix");
static_assert(offsetof(ClassLayout, SuperField) == 0x3C,
	"Unexpected UClass::SuperField offset");
static_assert(sizeof(BoundsLayout) == 0x1C, "Unexpected UI bounds layout");
static_assert(offsetof(ScreenObjectLayout, Position) == 0x3C,
	"Unexpected UIScreenObject::Position offset");
static_assert(offsetof(ScreenObjectLayout, Flags) == 0x68,
	"Unexpected UIScreenObject flags offset");
static_assert(offsetof(ScreenObjectLayout, Children) == 0x6C,
	"Unexpected UIScreenObject::Children offset");
static_assert(offsetof(UiObjectLayout, Owner) == 0x1C4,
	"Unexpected UIObject::Owner offset");
static_assert(offsetof(UiObjectLayout, OnValueChanged) == 0x39C,
	"Unexpected UIObject::OnValueChanged offset");
static_assert(sizeof(ScriptDelegateLayout) == 0x0C,
	"Unexpected FScriptDelegate layout");
static_assert(sizeof(RangeLayout) == 0x14, "Unexpected UIRangeData layout");
static_assert(offsetof(SliderLayout, SliderValue) == 0x464,
	"Unexpected UISlider::SliderValue offset");
static_assert(offsetof(PlayerControllerLayout, Pawn) == 0x1CC,
	"Unexpected AController::Pawn offset");
static_assert(offsetof(LocalPlayerLayout, Actor) == 0x40,
	"Unexpected UPlayer::Actor offset");
static_assert(offsetof(VideoSceneLayout, ApplyButton) == 0x1F0,
	"Unexpected VideoSettings::ApplyButton offset");
static_assert(offsetof(VideoSceneLayout, ResetButton) == 0x1F4,
	"Unexpected VideoSettings::ResetButton offset");
static_assert(offsetof(VideoSceneLayout, ComboBoxes) == 0x244,
	"Unexpected VideoSettings::ComboBoxes offset");
static_assert(offsetof(VideoSceneLayout, GammaCorrectionSlider) == 0x42C,
	"Unexpected VideoSettings::GammaCorrectionSlider offset");
static_assert(offsetof(InputEventParametersLayout, UiObjectReference) == 0,
	"Unexpected InputEventParameters::UIObjectReference offset");
static_assert(sizeof(StringLayout) == 0x0C, "Unexpected FString layout");
static_assert(offsetof(ConsoleCommandParametersLayout, WriteToLog) == 0x0C,
	"Unexpected ConsoleCommand bWriteToLog offset");
static_assert(offsetof(ConsoleCommandParametersLayout, ReturnValue) == 0x10,
	"Unexpected ConsoleCommand return offset");
static_assert(sizeof(ConsoleCommandParametersLayout) == 0x1C,
	"Unexpected ConsoleCommand parameter size");

using ProcessEventFunction =
	void(__thiscall*)(UObject*, UFunction*, void*, void*);
using CreateWidgetFunction = UiObjectLayout*(__thiscall*)(
	ScreenObjectLayout*, ScreenObjectLayout*, UClass*, UObject*, FName);
using GetPlayerOwnerFunction = LocalPlayerLayout*(__thiscall*)(
	ScreenObjectLayout*, int);
using InsertChildFunction = int(__thiscall*)(
	ScreenObjectLayout*, UiObjectLayout*, int, std::uint32_t);
using SliderGetValueFunction = float(__thiscall*)(SliderLayout*, std::uint32_t);
using SliderSetValueFunction = bool(__thiscall*)(SliderLayout*, float, std::uint32_t);
using DestroyStringFunction = void(__thiscall*)(StringLayout*);
struct ControlState {
	UiObjectLayout* Label = nullptr;
	SliderLayout* Slider = nullptr;
};

struct UiState {
	VideoSceneLayout* Scene = nullptr;
	SliderLayout* GammaSlider = nullptr;
	ControlState Fov;
	ControlState CombatText;
	bool Attempted = false;
	bool Ready = false;
};

UiState g_ui;
int g_preferredFov = 0;

UFunction* g_labelSetValueFunction = nullptr;
UFunction* g_setVisibilityFunction = nullptr;
UFunction* g_consoleCommandFunction = nullptr;

const TArray<UObject*>* GlobalObjects() {
	const auto* objects =
		reinterpret_cast<const TArray<UObject*>*>(kGlobalObjectsAddress);
	if (!objects->Data || objects->Count < 0 ||
		objects->Count > kMaximumObjectCount || objects->Max < objects->Count) {
		return nullptr;
	}
	return objects;
}

bool ObjectNameEquals(const UObject* object, const char* expected) {
	if (!object || !expected) return false;
	const char* name = object->Name.GetName();
	return name && std::strcmp(name, expected) == 0;
}

bool IsRegisteredObject(const UObject* object) {
	if (!object) return false;
	const TArray<UObject*>* objects = GlobalObjects();
	if (!objects) return false;
	for (int index = 0; index < objects->Count; ++index) {
		if (objects->Data[index] == object) return true;
	}
	return false;
}

UFunction* FindFunction(const char* ownerName, const char* functionName) {
	const TArray<UObject*>* objects = GlobalObjects();
	if (!objects) return nullptr;

	for (int index = 0; index < objects->Count; ++index) {
		UObject* object = objects->Data[index];
		if (!object ||
			!ObjectNameEquals(
				reinterpret_cast<const UObject*>(object->Class), "Function") ||
			!ObjectNameEquals(object, functionName) ||
			!ObjectNameEquals(object->Outer, ownerName) ||
			!ObjectNameEquals(object->Outer->Outer, "Engine")) {
			continue;
		}
		return reinterpret_cast<UFunction*>(object);
	}
	return nullptr;
}

bool IsA(const UObject* object, const char* className) {
	if (!object || !object->Class || !className) return false;
	const auto* current = reinterpret_cast<const ClassLayout*>(object->Class);
	for (int depth = 0; current && depth < 64; ++depth) {
		if (ObjectNameEquals(&current->Object, className)) return true;
		current = current->SuperField;
	}
	return false;
}

void ProcessEvent(UObject* object, UFunction* function, void* parameters) {
	auto processEvent =
		reinterpret_cast<ProcessEventFunction>(kProcessEventAddress);
	processEvent(object, function, parameters, nullptr);
}

bool ResolveUiFunctions() {
	if (!g_labelSetValueFunction) {
		g_labelSetValueFunction = FindFunction("UILabel", "SetValue");
	}
	if (!g_setVisibilityFunction) {
		g_setVisibilityFunction =
			FindFunction("UIScreenObject", "SetVisibility");
	}
	return g_labelSetValueFunction && g_setVisibilityFunction;
}

void SetVisible(UiObjectLayout* object, bool visible) {
	if (!object || !g_setVisibilityFunction) return;
	struct Parameters {
		std::uint32_t IsVisible;
	} parameters{visible ? 1u : 0u};
	ProcessEvent(&object->Screen.Object, g_setVisibilityFunction, &parameters);

	// Preserve the requested state if a script override declines to update it.
	if (visible) {
		object->Screen.Flags &= ~kHiddenFlag;
	} else {
		object->Screen.Flags |= kHiddenFlag;
	}
}

void SetLabelText(
	UiObjectLayout* label, const wchar_t* format, int value) {
	if (!label || !format || !g_labelSetValueFunction) return;
	wchar_t text[32]{};
	const int length = std::swprintf(
		text,
		sizeof(text) / sizeof(text[0]),
		format,
		value);
	if (length <= 0) return;
	struct Parameters {
		StringLayout NewText;
	} parameters{{text, length + 1, length + 1}};
	ProcessEvent(&label->Screen.Object, g_labelSetValueFunction, &parameters);
}

void SetFovLabelText(UiObjectLayout* label, int fov) {
	SetLabelText(label, kFovLabelFormat, FovSetting::Clamp(fov));
}

void SetCombatTextScalingLabelText(UiObjectLayout* label, int percent) {
	SetLabelText(
		label,
		kCombatTextScalingLabelFormat,
		CombatTextScaleSetting::Clamp(percent));
}

void SetSliderValue(SliderLayout* slider, float value) {
	auto setValue =
		reinterpret_cast<SliderSetValueFunction>(kSliderSetValueAddress);
	setValue(slider, value, 0);
}

float GetSliderValue(SliderLayout* slider) {
	auto getValue =
		reinterpret_cast<SliderGetValueFunction>(kSliderGetValueAddress);
	return getValue(slider, 0);
}

void HideNewWidget(UiObjectLayout* widget) {
	if (widget) widget->Screen.Flags |= kHiddenFlag;
}

void LogUiSetupFailure(const char* reason) {
	Logger::Log(
		"clientpatch",
		"[video-sliders] control setup failed: %s\n",
		reason);
}

bool PlaceInSliderRow(
	UiObjectLayout* widget, float left, float width,
	float rowWidth) {
	if (!widget || !std::isfinite(left) ||
		!std::isfinite(width) || !std::isfinite(rowWidth) ||
		left < 0.0f || width <= 0.0f || rowWidth <= 0.0f ||
		left > rowWidth - width ||
		!std::isfinite(widget->Screen.Position.Value[0]) ||
		!std::isfinite(widget->Screen.Position.Value[2])) {
		return false;
	}
	widget->Screen.Position.Value[0] = left;
	widget->Screen.Position.Value[2] = width;
	for (std::uint8_t& invalidated : widget->Screen.Position.Invalidated) {
		invalidated = 1;
	}
	return true;
}

bool DeriveColumnDelta(
	const VideoSceneLayout* scene,
	const SliderLayout* source,
	float* delta) {
	if (!scene || !source || !delta) return false;
	const BoundsLayout& sourceBounds = source->Object.Screen.Position;
	if (!std::isfinite(sourceBounds.Value[0]) ||
		!std::isfinite(sourceBounds.Value[2]) ||
		sourceBounds.ScaleType[0] != sourceBounds.ScaleType[2]) {
		return false;
	}

	bool found = false;
	float minimumLeft = 0.0f;
	float maximumLeft = 0.0f;
	int matchingCount = 0;
	for (UiObjectLayout* comboBox : scene->ComboBoxes) {
		if (!comboBox) continue;
		const BoundsLayout& bounds = comboBox->Screen.Position;
		if (bounds.ScaleType[0] != sourceBounds.ScaleType[0] ||
			!std::isfinite(bounds.Value[0])) {
			continue;
		}
		if (!found) {
			minimumLeft = maximumLeft = bounds.Value[0];
			found = true;
		} else {
			if (bounds.Value[0] < minimumLeft) minimumLeft = bounds.Value[0];
			if (bounds.Value[0] > maximumLeft) maximumLeft = bounds.Value[0];
		}
		++matchingCount;
	}

	const float width = sourceBounds.Value[2];
	const float candidate = maximumLeft - minimumLeft;
	if (matchingCount < 2 || !std::isfinite(width) || width <= 0.0f ||
		!std::isfinite(candidate) || candidate <= width) {
		return false;
	}
	*delta = candidate;
	return true;
}

bool DeriveSliderRowGeometry(
	const SliderLayout* source, float columnDelta,
	SliderRowGeometry* geometry) {
	if (!source || !geometry || !std::isfinite(columnDelta)) return false;
	const float sourceLeft = source->Object.Screen.Position.Value[0];
	const float width = source->Object.Screen.Position.Value[2];
	const float rowWidth = sourceLeft + columnDelta +
		width * kRightColumnWidthFraction;
	const float gapWidth = rowWidth - width * 3.0f;
	if (!std::isfinite(sourceLeft) || sourceLeft < 0.0f ||
		!std::isfinite(width) || width <= 0.0f ||
		!std::isfinite(rowWidth) || !std::isfinite(gapWidth) ||
		gapWidth <= 0.0f) {
		return false;
	}

	const float gap = gapWidth * 0.25f;
	*geometry = {
		gap,
		width + gap * 2.0f,
		width * 2.0f + gap * 3.0f,
		width,
		rowWidth,
	};
	return true;
}

UiObjectLayout* FindSliderLabel(SliderLayout* slider) {
	if (!slider) return nullptr;
	const TArray<UiObjectLayout*>& children = slider->Object.Screen.Children;
	if (!children.Data || children.Count < 0 ||
		children.Count > kMaximumChildrenPerWidget ||
		children.Max < children.Count) {
		return nullptr;
	}

	for (int index = 0; index < children.Count; ++index) {
		UiObjectLayout* child = children.Data[index];
		if (child && child->Owner == &slider->Object &&
			IsA(&child->Screen.Object, "UILabel")) {
			return child;
		}
	}
	return nullptr;
}

UiObjectLayout* CreateClone(
	VideoSceneLayout* scene,
	UiObjectLayout* source,
	UiObjectLayout* owner) {
	if (!scene || !source || !owner || !source->Screen.Object.Class) {
		return nullptr;
	}
	auto createWidget =
		reinterpret_cast<CreateWidgetFunction>(kCreateWidgetAddress);
	FName none{};
	return createWidget(
		reinterpret_cast<ScreenObjectLayout*>(scene),
		&owner->Screen,
		source->Screen.Object.Class,
		&source->Screen.Object,
		none);
}

bool InsertClone(UiObjectLayout* owner, UiObjectLayout* child) {
	if (!owner || !child) return false;
	auto** vtable = *reinterpret_cast<void***>(owner);
	if (!vtable) return false;
	auto insertChild = reinterpret_cast<InsertChildFunction>(
		vtable[kInsertChildVtableOffset / sizeof(void*)]);
	return insertChild && insertChild(&owner->Screen, child, -1, 1) >= 0;
}

int CurrentPreferredFov() {
	return g_preferredFov == 0
		? 0
		: FovSetting::Clamp(g_preferredFov);
}

bool IsPlayerController(const PlayerControllerLayout* controller) {
	return controller && IsRegisteredObject(&controller->Object) &&
		IsA(&controller->Object, "PlayerController");
}

bool HasGameplayPawn(const PlayerControllerLayout* controller) {
	return IsPlayerController(controller) && controller->Pawn &&
		IsRegisteredObject(controller->Pawn) && IsA(controller->Pawn, "Pawn");
}

PlayerControllerLayout* ResolveGameplayController(VideoSceneLayout* scene) {
	if (!scene || !IsRegisteredObject(&scene->Object)) return nullptr;
	auto getPlayerOwner =
		reinterpret_cast<GetPlayerOwnerFunction>(kGetPlayerOwnerAddress);
	LocalPlayerLayout* localPlayer = getPlayerOwner(
		reinterpret_cast<ScreenObjectLayout*>(scene), 0);
	if (!localPlayer || !IsRegisteredObject(&localPlayer->Object) ||
		!IsA(&localPlayer->Object, "LocalPlayer")) {
		return nullptr;
	}
	return HasGameplayPawn(localPlayer->Actor) ? localPlayer->Actor : nullptr;
}

bool ExecuteFovCommand(PlayerControllerLayout* controller, int fov) {
	if (!IsPlayerController(controller)) return false;
	if (!g_consoleCommandFunction) {
		g_consoleCommandFunction =
			FindFunction("PlayerController", "ConsoleCommand");
	}
	if (!g_consoleCommandFunction) return false;

	wchar_t command[16]{};
	const int length = std::swprintf(
		command,
		sizeof(command) / sizeof(command[0]),
		L"FOV %d",
		FovSetting::Clamp(fov));
	if (length <= 0) return false;

	ConsoleCommandParametersLayout parameters{
		{command, length + 1, length + 1},
		0,
		{}};
	ProcessEvent(&controller->Object, g_consoleCommandFunction, &parameters);
	if (parameters.ReturnValue.Data && parameters.ReturnValue.Data != command) {
		auto destroyString =
			reinterpret_cast<DestroyStringFunction>(kDestroyStringAddress);
		destroyString(&parameters.ReturnValue);
	}
	return true;
}

void ConfigureSlider(SliderLayout* slider, int minimum, int maximum) {
	slider->SliderValue.MinimumValue = static_cast<float>(minimum);
	slider->SliderValue.MaximumValue = static_cast<float>(maximum);
	slider->SliderValue.NudgeValue = 1.0f;
	slider->SliderValue.Flags = 1;
}

bool IsControlReady(
	const ControlState& control, const VideoSceneLayout* scene) {
	return scene && control.Label && control.Slider &&
		IsRegisteredObject(&control.Label->Screen.Object) &&
		IsRegisteredObject(&control.Slider->Object.Screen.Object) &&
		control.Slider->Object.OnValueChanged.Object == &scene->Object;
}

bool CreateSliderControl(
	VideoSceneLayout* scene,
	SliderLayout* sourceSlider,
	UiObjectLayout* sourceLabel,
	float left,
	float width,
	float rowWidth,
	int minimum,
	int maximum,
	int initialValue,
	ControlState* result) {
	if (!scene || !sourceSlider || !sourceLabel ||
		!sourceSlider->Object.Owner || !result) {
		return false;
	}
	UiObjectLayout* sliderObject = CreateClone(
		scene, &sourceSlider->Object, sourceSlider->Object.Owner);
	if (!sliderObject || sliderObject == &sourceSlider->Object ||
		!IsA(&sliderObject->Screen.Object, "UISlider")) {
		return false;
	}
	auto* slider = reinterpret_cast<SliderLayout*>(sliderObject);
	HideNewWidget(sliderObject);
	sliderObject->OnValueChanged = {};
	ConfigureSlider(slider, minimum, maximum);
	if (!PlaceInSliderRow(sliderObject, left, width, rowWidth) ||
		!InsertClone(sourceSlider->Object.Owner, sliderObject)) {
		return false;
	}
	SetSliderValue(slider, static_cast<float>(initialValue));

	UiObjectLayout* label = CreateClone(scene, sourceLabel, sliderObject);
	if (!label || label == sourceLabel) return false;
	HideNewWidget(label);
	if (!InsertClone(sliderObject, label)) return false;

	result->Label = label;
	result->Slider = slider;
	return true;
}

void EnsureUi(VideoSceneLayout* scene) {
	if (!scene || !IsRegisteredObject(
			reinterpret_cast<const UObject*>(scene))) {
		if (g_ui.Scene == scene) g_ui = {};
		return;
	}
	if (!scene->GammaCorrectionSlider) return;
	SliderLayout* gammaSlider = scene->GammaCorrectionSlider;

	if (g_ui.Scene == scene && g_ui.GammaSlider == gammaSlider) {
		if (g_ui.Ready && IsControlReady(g_ui.Fov, scene) &&
			IsControlReady(g_ui.CombatText, scene)) {
			return;
		}
		if (g_ui.Attempted) return;
	} else {
		g_ui = {};
		g_ui.Scene = scene;
		g_ui.GammaSlider = gammaSlider;
	}

	g_ui.Attempted = true;
	if (!gammaSlider->Object.Owner ||
		!IsA(&gammaSlider->Object.Screen.Object, "UISlider") ||
		gammaSlider->Object.OnValueChanged.Object != &scene->Object ||
		!ResolveUiFunctions()) {
		LogUiSetupFailure("retail UI primitives unavailable");
		return;
	}

	UiObjectLayout* sourceLabel = FindSliderLabel(gammaSlider);
	float columnDelta = 0.0f;
	SliderRowGeometry rowGeometry = {};
	if (!sourceLabel || sourceLabel->Owner != &gammaSlider->Object ||
		!DeriveColumnDelta(scene, gammaSlider, &columnDelta) ||
		!DeriveSliderRowGeometry(
			gammaSlider, columnDelta, &rowGeometry)) {
		LogUiSetupFailure("Brightness layout not verified");
		return;
	}
	if (!PlaceInSliderRow(
			&gammaSlider->Object,
			rowGeometry.brightnessLeft,
			rowGeometry.controlWidth,
			rowGeometry.rowWidth)) {
		LogUiSetupFailure("Brightness placement rejected");
		return;
	}

	const int preferred = CurrentPreferredFov();
	const int initialFov = preferred == 0
		? FovSetting::kDefaultFov
		: preferred;
	const int initialCombatTextScale = CombatTextScaleSetting::Clamp(
		ClientCombatTextScalePatch::ScalePercent());
	ControlState fov;
	if (!CreateSliderControl(
			scene,
			gammaSlider,
			sourceLabel,
			rowGeometry.fovLeft,
			rowGeometry.controlWidth,
			rowGeometry.rowWidth,
			FovSetting::kDefaultFov,
			FovSetting::kMaximumFov,
			initialFov,
			&fov)) {
		LogUiSetupFailure("FOV control creation rejected");
		return;
	}
	ControlState combatText;
	if (!CreateSliderControl(
			scene,
			gammaSlider,
			sourceLabel,
			rowGeometry.combatTextLeft,
			rowGeometry.controlWidth,
			rowGeometry.rowWidth,
			CombatTextScaleSetting::kMinimumPercent,
			CombatTextScaleSetting::kMaximumPercent,
			initialCombatTextScale,
			&combatText)) {
		LogUiSetupFailure("combat-text control creation rejected");
		return;
	}

	SetFovLabelText(fov.Label, initialFov);
	SetCombatTextScalingLabelText(combatText.Label, initialCombatTextScale);
	g_ui.Fov = fov;
	g_ui.CombatText = combatText;
	fov.Slider->Object.OnValueChanged = gammaSlider->Object.OnValueChanged;
	combatText.Slider->Object.OnValueChanged =
		gammaSlider->Object.OnValueChanged;
	g_ui.Ready = true;
	SetVisible(fov.Label, true);
	SetVisible(&fov.Slider->Object, true);
	SetVisible(combatText.Label, true);
	SetVisible(&combatText.Slider->Object, true);
}

bool PersistFovPreference(int fov) {
	return EngineConfig::SaveInt(
		kConfigSection, kFovConfigValue, FovSetting::Clamp(fov));
}

int SelectedFov(VideoSceneLayout* scene) {
	if (!scene || !g_ui.Ready || g_ui.Scene != scene || !g_ui.Fov.Slider) {
		return 0;
	}
	return FovSetting::FromSlider(GetSliderValue(g_ui.Fov.Slider));
}

int SelectedCombatTextScale(VideoSceneLayout* scene) {
	if (!scene || !g_ui.Ready || g_ui.Scene != scene ||
		!g_ui.CombatText.Slider) {
		return 0;
	}
	return CombatTextScaleSetting::FromSlider(
		GetSliderValue(g_ui.CombatText.Slider));
}

void ApplyFovPreference(
	VideoSceneLayout* scene,
	PlayerControllerLayout* controller,
	int fov) {
	if (fov == 0) return;
	fov = FovSetting::Clamp(fov);
	g_preferredFov = fov;
	if (g_ui.Ready && g_ui.Scene == scene &&
		IsControlReady(g_ui.Fov, scene)) {
		ConfigureSlider(
			g_ui.Fov.Slider,
			FovSetting::kDefaultFov,
			FovSetting::kMaximumFov);
		SetSliderValue(g_ui.Fov.Slider, static_cast<float>(fov));
		SetFovLabelText(g_ui.Fov.Label, fov);
	}

	const bool saved = PersistFovPreference(fov);
	if (!saved) {
		Logger::Log(
			"clientpatch",
			"[fov-slider] preference save failed: engine config unavailable\n");
	}

	[[maybe_unused]] const bool commandSent =
		HasGameplayPawn(controller) && ExecuteFovCommand(controller, fov);
#ifdef GA_CLIENT_DEBUG
	Logger::Log(
		"clientpatch",
		"[fov-slider] Apply: value=%d saved=%s command=%s\n",
		fov,
		saved ? "yes" : "no",
		commandSent ? "sent" : "skipped");
#endif
}

void ApplyCombatTextPreference(VideoSceneLayout* scene, int percent) {
	if (percent == 0) return;
	percent = CombatTextScaleSetting::Clamp(percent);
	ClientCombatTextScalePatch::ApplyScalePercent(percent);
	if (g_ui.Ready && g_ui.Scene == scene &&
		IsControlReady(g_ui.CombatText, scene)) {
		ConfigureSlider(
			g_ui.CombatText.Slider,
			CombatTextScaleSetting::kMinimumPercent,
			CombatTextScaleSetting::kMaximumPercent);
		SetSliderValue(
			g_ui.CombatText.Slider, static_cast<float>(percent));
		SetCombatTextScalingLabelText(g_ui.CombatText.Label, percent);
	}
}

void ResetSlidersToDefaults(VideoSceneLayout* scene) {
	if (!scene || !g_ui.Ready || g_ui.Scene != scene) return;
	if (IsControlReady(g_ui.Fov, scene)) {
		ConfigureSlider(
			g_ui.Fov.Slider,
			FovSetting::kDefaultFov,
			FovSetting::kMaximumFov);
		SetSliderValue(
			g_ui.Fov.Slider, static_cast<float>(FovSetting::kDefaultFov));
		SetFovLabelText(g_ui.Fov.Label, FovSetting::kDefaultFov);
	}
	if (IsControlReady(g_ui.CombatText, scene)) {
		ConfigureSlider(
			g_ui.CombatText.Slider,
			CombatTextScaleSetting::kMinimumPercent,
			CombatTextScaleSetting::kMaximumPercent);
		SetSliderValue(
			g_ui.CombatText.Slider,
			static_cast<float>(CombatTextScaleSetting::kDefaultPercent));
		SetCombatTextScalingLabelText(
			g_ui.CombatText.Label,
			CombatTextScaleSetting::kDefaultPercent);
	}
}

bool SyncLabelFromSlider(UiObjectLayout* sender) {
	if (!g_ui.Ready || !sender) return false;
	if (g_ui.Fov.Slider && sender == &g_ui.Fov.Slider->Object) {
		const int fov =
			FovSetting::FromSlider(GetSliderValue(g_ui.Fov.Slider));
		SetFovLabelText(g_ui.Fov.Label, fov);
		return true;
	}
	if (g_ui.CombatText.Slider &&
		sender == &g_ui.CombatText.Slider->Object) {
		const int percent = CombatTextScaleSetting::FromSlider(
			GetSliderValue(g_ui.CombatText.Slider));
		SetCombatTextScalingLabelText(g_ui.CombatText.Label, percent);
		return true;
	}
	return false;
}

class VideoGammaSliderChangedHook : public HookBase<
	void(__fastcall*)(VideoSceneLayout*, void*, UiObjectLayout*, int),
	0x11499640u,
	VideoGammaSliderChangedHook> {
public:
	static void __fastcall Call(
		VideoSceneLayout* scene,
		void* edx,
		UiObjectLayout* sender,
		int playerIndex) {
		if (g_ui.Ready && g_ui.Scene == scene &&
			SyncLabelFromSlider(sender)) return;
		m_original(scene, edx, sender, playerIndex);
	}
};

class VideoFixupHook : public HookBase<
	void(__fastcall*)(VideoSceneLayout*, void*),
	0x114A1830u,
	VideoFixupHook> {
public:
	static void __fastcall Call(VideoSceneLayout* scene, void* edx) {
		m_original(scene, edx);
		EnsureUi(scene);
	}
};

class VideoTickHook : public HookBase<
	int(__fastcall*)(VideoSceneLayout*, void*),
	0x114996A0u,
	VideoTickHook> {
public:
	static int __fastcall Call(VideoSceneLayout* scene, void* edx) {
		const int result = m_original(scene, edx);
		if (!scene || g_ui.Scene != scene ||
			g_ui.GammaSlider != scene->GammaCorrectionSlider ||
			(!g_ui.Ready && !g_ui.Attempted)) {
			EnsureUi(scene);
		}
		return result;
	}
};

class VideoButtonHook : public HookBase<
	int(__fastcall*)(VideoSceneLayout*, void*, InputEventParametersLayout*),
	0x114A4050u,
	VideoButtonHook> {
public:
	static int __fastcall Call(
		VideoSceneLayout* scene,
		void* edx,
		InputEventParametersLayout* eventParameters) {
		UiObjectLayout* target =
			eventParameters ? eventParameters->UiObjectReference : nullptr;
		const bool apply = scene && target == scene->ApplyButton;
		const bool reset = scene && target == scene->ResetButton;
		const int selectedFov = apply ? SelectedFov(scene) : 0;
		const int selectedCombatTextScale =
			apply ? SelectedCombatTextScale(scene) : 0;
		PlayerControllerLayout* controller =
			apply ? ResolveGameplayController(scene) : nullptr;
		const int handled = m_original(scene, edx, eventParameters);
		if (handled && scene) {
			if (apply) {
				ApplyFovPreference(scene, controller, selectedFov);
				ApplyCombatTextPreference(scene, selectedCombatTextScale);
			}
			if (reset) ResetSlidersToDefaults(scene);
		}
		return handled;
	}
};

// SetPlayer finishes by dispatching ReceivedPlayer after linking the local
// player and controller, so its post-call path is the stable instance boundary.
class PlayerSetPlayerHook : public HookBase<
	void(__fastcall*)(
		PlayerControllerLayout*, void*, LocalPlayerLayout*),
	0x10C54DA0u,
	PlayerSetPlayerHook> {
public:
	static void __fastcall Call(
		PlayerControllerLayout* controller,
		void* edx,
		LocalPlayerLayout* player) {
		m_original(controller, edx, player);
		if (!player || !IsRegisteredObject(&player->Object) ||
			!IsA(&player->Object, "LocalPlayer") ||
			player->Actor != controller || !IsPlayerController(controller)) {
			return;
		}

		const int preferred = CurrentPreferredFov();
		if (preferred != 0 && ExecuteFovCommand(controller, preferred)) {
#ifdef GA_CLIENT_DEBUG
			Logger::Log(
				"clientpatch",
				"[fov-slider] reapplied after local player attachment: value=%d\n",
				preferred);
#endif
		}
	}
};

}  // namespace

void ClientFovSliderPatch::Initialize() {
	int savedFov = 0;
	const EngineConfig::LoadIntResult result = EngineConfig::LoadInt(
		kConfigSection, kFovConfigValue, &savedFov);
	if (result == EngineConfig::LoadIntResult::Loaded) {
		g_preferredFov = FovSetting::Clamp(savedFov);
	} else if (result == EngineConfig::LoadIntResult::Invalid) {
		Logger::Log(
			"clientpatch",
			"[fov-slider] invalid saved preference ignored\n");
	}
}

LONG ClientFovSliderPatch::Install() {
	LONG result = PlayerSetPlayerHook::Install();
	if (result == NO_ERROR) result = VideoFixupHook::Install();
	if (result == NO_ERROR) result = VideoTickHook::Install();
	if (result == NO_ERROR) result = VideoGammaSliderChangedHook::Install();
	if (result == NO_ERROR) result = VideoButtonHook::Install();
	return result;
}
