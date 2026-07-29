#pragma once

#include <string>

namespace ClientLogDirectory {

// Select the release logs root or initialize the DEBUG process/startup
// hierarchy before channels and crash handlers are enabled.
bool Initialize(const std::string& logsRoot);

#ifdef GA_CLIENT_DEBUG
// Rotate normal and crash output to a fresh child of the current process root.
// ReceivedPlayer is the sole caller and therefore owns instance sequencing.
bool BeginInstance();
#endif

}  // namespace ClientLogDirectory
