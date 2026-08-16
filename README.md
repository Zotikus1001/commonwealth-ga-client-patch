# Commonwealth Global Agenda Client Patch

A 32-bit `dinput8.dll` proxy for the reviewed Global Agenda 1.5.1.5 retail
client. It forwards the normal DirectInput exports and applies local client
fixes after verifying the executable. Unsupported executables continue without
installing hooks.

All bundled changes are client-side and require no server support.

## Included changes

### Bug fixes

- **Audio update optimization** bounds redundant source management and reduces
  selected voices during sustained audio saturation.
- **Morph rebuild optimization** removes zero-weight morph targets before UE3
  rebuilds morph vertex buffers.
- **Scoped weapon visibility** prevents the local in-hand weapon mesh from
  being redundantly hidden and shown while scoped.
- **Jetpack aim alignment** keeps weapon aim aligned with the player view and
  rejects inversion-class view roll during flight.
- **Automatic F2 stats scaling** keeps the performance overlay readable above
  its 1080p baseline.
- **Friendly overhead-label normalization** keeps player, agency, and alliance
  labels proportional above 1080p.

### Features

- **Field of view slider** in the Video settings.
- **Combat Text Scaling slider** in the Video settings.
- **Spectator nameplates** show names, team colors, and health above live
  players while spectating.

## Building

### Windows

Run `windows-server-menu.bat` and choose the Debug or Release build. The script
can install MSYS2 and the required 32-bit MinGW compiler when they are missing.

### Linux or MSYS2

Install GNU Make, a native C++ compiler for tests, and the 32-bit MinGW-w64
cross-compiler. On Debian or Ubuntu:

```bash
sudo apt install make g++ g++-mingw-w64-i686
```

Build with one of these commands:

```bash
bash build-client-patch.sh debug
bash build-client-patch.sh release
bash build-client-patch.sh package-release
```

The equivalent Make targets are `make debug`, `make release`, and
`make package-release`. The client DLL is written to
`out/clientpatch/dinput8.dll`; the packaged release is written to
`out/distribution/Commonwealth-GA-Client-Patches-x86.dll`.

Run the host test suite with:

```bash
make test
```

## Server-gated feature integration

The repository includes a one-way compatibility gate for future features that
need server cooperation. No bundled change currently uses it.

The server sends a token through the stock reliable
`APlayerController::ClientCapBandwidth(int)` RPC. The token contains the magic
byte `0x6D`, a stable nonzero feature ID, and a nonzero compatibility release.
The client activates only an exact registered match and resets activation on
travel. The protocol sends no client response, heartbeat, or probe.

### Client

Register the feature in `src/Handshake/ClientFeatureRegistry.cpp` and propagate
registration failure:

```cpp
#include "src/Handshake/FeatureRegistry.hpp"

namespace {
constexpr ClientFeatureMagic::FeatureId kFeatureId = 1;
constexpr ClientFeatureMagic::FeatureRelease kFeatureRelease = 1;

bool SetFeatureAvailable(bool active) {
	// Enable or disable only this feature's runtime state.
	return true;
}
}

bool RegisterClientFeatures() {
	return ClientFeatureHandshake::Register({
		kFeatureId,
		kFeatureRelease,
		"feature-name",
		&SetFeatureAvailable,
	});
}
```

Keep the feature inert unless
`ClientFeatureHandshake::IsActive(kFeatureId)` is true. Release builds install
the carrier hook only when the registry contains at least one feature, so the
registration must ship before the server advertises it.

### Server

Mirror or vendor `src/Handshake/FeatureMagic.hpp`, then advertise the matching
token after login and after every instance join:

```cpp
playerController->ClientCapBandwidth(
	ClientFeatureMagic::MakeToken(kFeatureId, kFeatureRelease));
```

Never reuse an ID for an unrelated feature. Bump the release whenever the
client/server behavior or data contract becomes incompatible. The one-way gate
does not prove to the server that the DLL is installed or that activation
succeeded.

Contributor requirements are in `AGENTS.md` and `CLAUDE.md`.
