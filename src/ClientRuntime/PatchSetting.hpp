#pragma once

// Per-patch on/off switches the launcher (or a player) sets in TgGame\Config\TgEngine.ini:
//
//   [Commonwealth.ClientPatch]
//   RenderCommandSignal=1
//
// Read once while hooks are installed; a change applies at the next game start. A disabled patch installs no hooks.
namespace PatchSetting {

bool Enabled(const char* key, bool defaultEnabled);

}  // namespace PatchSetting
