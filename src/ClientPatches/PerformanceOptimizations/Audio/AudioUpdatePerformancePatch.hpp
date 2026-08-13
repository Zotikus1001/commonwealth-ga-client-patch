#pragma once

#include "src/pch.hpp"

// Reduces redundant game-thread source management without changing the
// initialized OpenAL source pool or moving engine-owned objects across threads.
class ClientAudioUpdatePerformancePatch {
public:
	static LONG Install();
};
