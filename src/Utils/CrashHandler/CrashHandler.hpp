#pragma once

namespace CrashHandler {
// Install fatal handlers after Logger has published the initial log directory.
// Each crash snapshots Logger's current immutable directory, so DEBUG
// instance rotation also moves crash output without mutating fatal-path state.
void Install();
}
