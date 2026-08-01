#pragma once

#include "src/pch.hpp"

// Local Video-settings extension. All UI ownership, persistence, ABI views,
// and travel-reset behavior remain inside this patch.
class ClientFovSliderPatch {
public:
	static void Initialize();
	static LONG Install();
};
