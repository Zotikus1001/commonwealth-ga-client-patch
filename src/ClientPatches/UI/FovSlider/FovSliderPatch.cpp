#include "src/ClientPatches/UI/FovSlider/FovSliderPatch.hpp"

#include "src/ClientPatches/UI/FovSlider/FovSetting.hpp"
#include "src/Utils/HookBase.hpp"
#include "src/Utils/Logger/Logger.hpp"

#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr std::uintptr_t kGlobalObjectsAddress = 0x13465A54u;
constexpr std::uintptr_t kProcessEventAddress = 0x11347C20u;
constexpr std::uintptr_t kCreateWidgetAddress = 0x10B7E800u;
constexpr std::uintptr_t kGetPlayerOwnerAddress = 0x10B5E360u;
constexpr std::uintptr_t kSliderGetValueAddress = 0x10B5AF00u;
constexpr std::uintptr_t kSliderSetValueAddress = 0x10B5CC90u;
constexpr std::uintptr_t kDestroyStringAddress = 0x112C18C0u;
constexpr std::uintptr_t kConfigSetIntAddress = 0x1131EFF0u;
constexpr std::uintptr_t kConfigFlushAddress = 0x1131C880u;
constexpr std::uintptr_t kGlobalConfigAddress = 0x134237DCu;
constexpr std::uintptr_t kEngineIniAddress = 0x1342C0A8u;
constexpr std::size_t kInsertChildVtableOffset = 0x128u;
constexpr std::uint32_t kHiddenFlag = 0x1u;
constexpr int kMaximumObjectCount = 1000000;
constexpr int kMaximumChildrenPerWidget = 4096;

constexpr wchar_t kConfigSection[] = L"Commonwealth.ClientPatch";
constexpr wchar_t kConfigValue[] = L"FieldOfView";

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
using ConfigSetIntFunction = void(__thiscall*)(
	void*, const wchar_t*, const wchar_t*, int, const wchar_t*);
using ConfigFlushFunction = void(__thiscall*)(void*, int, const wchar_t*);

struct UiState {
	VideoSceneLayout* Scene = nullptr;
	SliderLayout* GammaSlider = nullptr;
	UiObjectLayout* Label = nullptr;
	SliderLayout* Slider = nullptr;
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
	if (!object || !object->Class) return false;
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

void SetLabelText(UiObjectLayout* label, int fov) {
	if (!label || !g_labelSetValueFunction) return;
	wchar_t value[16]{};
	const int length = std::swprintf(
		value,
		sizeof(value) / sizeof(value[0]),
		L"FOV: %d",
		FovSetting::Clamp(fov));
	if (length <= 0) return;
	struct Parameters {
		StringLayout NewText;
	} parameters{{value, length + 1, length + 1}};
	ProcessEvent(&label->Screen.Object, g_labelSetValueFunction, &parameters);
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
		"[fov-slider] Video control setup failed: %s\n",
		reason);
}

bool ShiftRight(UiObjectLayout* widget, float delta) {
	if (!widget || !std::isfinite(delta) ||
		!std::isfinite(widget->Screen.Position.Value[0])) {
		return false;
	}
	widget->Screen.Position.Value[0] += delta;
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

void ConfigureSlider(SliderLayout* slider) {
	slider->SliderValue.MinimumValue =
		static_cast<float>(FovSetting::kDefaultFov);
	slider->SliderValue.MaximumValue =
		static_cast<float>(FovSetting::kMaximumFov);
	slider->SliderValue.NudgeValue = 1.0f;
	slider->SliderValue.Flags = 1;
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
		if (g_ui.Ready && g_ui.Label && g_ui.Slider &&
			IsRegisteredObject(&g_ui.Label->Screen.Object) &&
			IsRegisteredObject(&g_ui.Slider->Object.Screen.Object) &&
			g_ui.Slider->Object.OnValueChanged.Object == &scene->Object) {
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
	if (!sourceLabel || sourceLabel->Owner != &gammaSlider->Object ||
		!DeriveColumnDelta(scene, gammaSlider, &columnDelta)) {
		LogUiSetupFailure("Brightness layout not verified");
		return;
	}

	UiObjectLayout* sliderObject =
		CreateClone(scene, &gammaSlider->Object, gammaSlider->Object.Owner);
	if (!sliderObject || sliderObject == &gammaSlider->Object ||
		!IsA(&sliderObject->Screen.Object, "UISlider")) {
		LogUiSetupFailure("slider clone unavailable");
		return;
	}
	auto* slider = reinterpret_cast<SliderLayout*>(sliderObject);
	HideNewWidget(sliderObject);
	sliderObject->OnValueChanged = {};

	const int preferred = CurrentPreferredFov();
	const float initialValue = preferred == 0
		? static_cast<float>(FovSetting::kDefaultFov)
		: static_cast<float>(preferred);
	ConfigureSlider(slider);
	if (!ShiftRight(sliderObject, columnDelta) ||
		!InsertClone(gammaSlider->Object.Owner, sliderObject)) {
		LogUiSetupFailure("slider placement rejected");
		return;
	}
	SetSliderValue(slider, initialValue);

	UiObjectLayout* label = CreateClone(scene, sourceLabel, sliderObject);
	if (!label || label == sourceLabel) {
		LogUiSetupFailure("label clone unavailable");
		return;
	}
	HideNewWidget(label);
	if (!InsertClone(sliderObject, label)) {
		LogUiSetupFailure("label placement rejected");
		return;
	}
	SetLabelText(label, static_cast<int>(initialValue));

	g_ui.Label = label;
	g_ui.Slider = slider;
	g_ui.Ready = true;
	sliderObject->OnValueChanged = gammaSlider->Object.OnValueChanged;
	SetVisible(label, true);
	SetVisible(sliderObject, true);
}

std::wstring ResolveConfigPath() {
	std::vector<wchar_t> executablePath(MAX_PATH);
	DWORD pathLength = 0;
	for (;;) {
		pathLength = GetModuleFileNameW(
			nullptr,
			executablePath.data(),
			static_cast<DWORD>(executablePath.size()));
		if (pathLength == 0) return {};
		if (pathLength < executablePath.size()) break;
		if (executablePath.size() >= 32768) return {};
		executablePath.resize(executablePath.size() * 2);
	}

	std::wstring path(executablePath.data(), pathLength);
	const std::size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos) return {};
	path.resize(slash);
	path += L"\\..\\TgGame\\Config\\TgEngine.ini";
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES ||
		(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		return {};
	}
	return path;
}

bool PersistPreference(int fov) {
	void* const configCache =
		*reinterpret_cast<void**>(kGlobalConfigAddress);
	const auto* const engineIni =
		reinterpret_cast<const wchar_t*>(kEngineIniAddress);
	if (!configCache || engineIni[0] == L'\0') return false;

	// Retail graphics Apply saves through this cache. A direct INI write is
	// overwritten when the engine later flushes its older cached contents.
	const auto setInt =
		reinterpret_cast<ConfigSetIntFunction>(kConfigSetIntAddress);
	const auto flush =
		reinterpret_cast<ConfigFlushFunction>(kConfigFlushAddress);
	setInt(
		configCache,
		kConfigSection,
		kConfigValue,
		FovSetting::Clamp(fov),
		engineIni);
	flush(configCache, 0, engineIni);
	return true;
}

int SelectedSliderFov(VideoSceneLayout* scene) {
	if (!scene || !g_ui.Ready || g_ui.Scene != scene || !g_ui.Slider) return 0;
	return FovSetting::FromSlider(GetSliderValue(g_ui.Slider));
}

void ApplySliderPreference(
	VideoSceneLayout* scene,
	PlayerControllerLayout* controller,
	int fov) {
	if (fov == 0) return;
	fov = FovSetting::Clamp(fov);
	g_preferredFov = fov;
	if (g_ui.Ready && g_ui.Scene == scene && g_ui.Label && g_ui.Slider &&
		IsRegisteredObject(&g_ui.Label->Screen.Object) &&
		IsRegisteredObject(&g_ui.Slider->Object.Screen.Object)) {
		ConfigureSlider(g_ui.Slider);
		SetSliderValue(g_ui.Slider, static_cast<float>(fov));
		SetLabelText(g_ui.Label, fov);
	}

	const bool saved = PersistPreference(fov);
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

void ResetSliderToRetailDefault(VideoSceneLayout* scene) {
	if (!scene || !g_ui.Ready || g_ui.Scene != scene || !g_ui.Slider) return;
	const float value = static_cast<float>(FovSetting::kDefaultFov);
	ConfigureSlider(g_ui.Slider);
	SetSliderValue(g_ui.Slider, value);
	SetLabelText(g_ui.Label, FovSetting::kDefaultFov);
}

void SyncLabelFromSlider(UiObjectLayout* sender) {
	if (!g_ui.Ready || !g_ui.Label || !g_ui.Slider ||
		sender != &g_ui.Slider->Object) {
		return;
	}
	const int fov = FovSetting::FromSlider(GetSliderValue(g_ui.Slider));
	SetLabelText(g_ui.Label, fov);
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
		if (g_ui.Ready && g_ui.Scene == scene && g_ui.Slider &&
			sender == &g_ui.Slider->Object) {
			SyncLabelFromSlider(sender);
			return;
		}
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
		const int selectedFov = apply ? SelectedSliderFov(scene) : 0;
		PlayerControllerLayout* controller =
			apply ? ResolveGameplayController(scene) : nullptr;
		const int handled = m_original(scene, edx, eventParameters);
		if (handled && scene) {
			if (apply) ApplySliderPreference(scene, controller, selectedFov);
			if (reset) ResetSliderToRetailDefault(scene);
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
	const std::wstring configPath = ResolveConfigPath();
	if (configPath.empty()) {
		Logger::Log(
			"clientpatch",
			"[fov-slider] TgEngine.ini unavailable; saved preference not loaded\n");
		return;
	}

	wchar_t value[16]{};
	const DWORD length = GetPrivateProfileStringW(
		kConfigSection,
		kConfigValue,
		L"",
		value,
		static_cast<DWORD>(sizeof(value) / sizeof(value[0])),
		configPath.c_str());
	if (length == 0) return;
	wchar_t* end = nullptr;
	const long parsed = std::wcstol(value, &end, 10);
	if (end == value || !end || *end != L'\0') {
		Logger::Log(
			"clientpatch",
			"[fov-slider] invalid saved preference ignored\n");
		return;
	}
	g_preferredFov = FovSetting::Clamp(static_cast<int>(parsed));
}

LONG ClientFovSliderPatch::Install() {
	LONG result = PlayerSetPlayerHook::Install();
	if (result == NO_ERROR) result = VideoFixupHook::Install();
	if (result == NO_ERROR) result = VideoTickHook::Install();
	if (result == NO_ERROR) result = VideoGammaSliderChangedHook::Install();
	if (result == NO_ERROR) result = VideoButtonHook::Install();
	return result;
}
