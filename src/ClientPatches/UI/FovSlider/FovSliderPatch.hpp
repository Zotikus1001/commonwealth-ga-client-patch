#pragma once

#include "src/pch.hpp"

// Owns the shared retail Video-menu hooks, slider composition, and FOV
// lifecycle.
class ClientFovSliderPatch {
public:
	static void Initialize();
	static LONG Install();
};
