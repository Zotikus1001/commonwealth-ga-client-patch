#pragma once

// Single registration entry point for opt-in, server-dependent client
// features. It runs once before the Detours transaction. Local-only fixes must
// not be added because that would unnecessarily install the ProcessEvent hook.
bool RegisterClientFeatures();
