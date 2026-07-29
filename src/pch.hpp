#pragma once

// Shared Win32, Detours, C runtime, and small standard-library surface used by
// patch translation units. Feature-specific headers stay out of this file so
// adding one patch does not broaden every compilation unit's dependencies.
#include <windows.h>

#ifndef __C_ASSERT__
#define __C_ASSERT__(x) typedef char __C_ASSERT__[(x) ? 1 : -1]
#endif
#include "lib/detours/detours.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "src/ClientRuntime/EngineLayouts.hpp"
