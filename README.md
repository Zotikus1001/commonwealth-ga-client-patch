# Commonwealth Global Agenda Client Patch

A standalone 32-bit `dinput8.dll` proxy for the reviewed Global Agenda
1.5.1.5 retail client.

## Included client fixes

- **Morph rebuild optimization** removes zero-weight morph targets from the
  render-thread update copy before UE3 rebuilds the morph vertex buffer.
- **Scoped weapon visibility** prevents the local in-hand weapon mesh from
  being shown and hidden again every tick while scoped.

## Included client features

- **Field of view slider** in the in-game Video settings.
- **Combat text scaling slider** for damage/healing, reticle pool values, and
  the enemy target marker.
- **Automatic F2 stats scaling** relative to a 1080p baseline.
- **Friendly overhead-name normalization** above 1080p.

These changes are local-only. They install without a cooperating server and are
not registered with the feature gate.

The DLL verifies the executable PE timestamp, image size, preferred and runtime
image base, and complete SHA-256 digest of its `.text` section before attaching
any hook. An unsupported executable is logged and otherwise left untouched.

## Build

### Windows

Run `windows-server-menu.bat`. Its only build choices are:

1. Debug: symbols and detailed patch profiling.
2. Release: optimized and stripped, retaining patch-status and crash logging.

After each instance join on a supported executable, a successfully installed
debug build writes a local blue Instance-chat summary of loaded local fixes and
every registered server-gated feature. Release builds do not show this
diagnostic.

The menu installs MSYS2 and the 32-bit MinGW compiler through
`winget`/`pacman` if needed.

### Linux

Install GNU Make, a native C++ compiler for tests, and the 32-bit MinGW-w64
cross-compiler. On Debian or Ubuntu:

```bash
sudo apt install make g++ g++-mingw-w64-i686
```

Then run either:

```bash
bash build-client-patch.sh debug
bash build-client-patch.sh release
```

The equivalent direct Make targets are `make debug` and `make release`.
Both platforms produce:

```text
out/clientpatch/dinput8.dll
```

The Windows menu and shell builder pass Make the number of processors
available to the current process; neither uses a fixed parallel-job count.

Run the platform-neutral registry test with `make test`.

## Distribute through GitHub Actions

The launcher reads releases from
`Zotikus1001/commonwealth-ga-client-patches` and accepts only the exact asset
name:

```text
Commonwealth-GA-Client-Patches-x86.dll
```

The manually triggered **Distribute release client patch DLL** workflow runs
the `package-release` target first. It performs a clean optimized build and
stages that exact asset under `out/distribution/`. The Actions-only publisher
rejects a non-x86 PE file, embedded debug sections, a remaining debug map,
missing repository write permission, and an existing release tag. It creates
a draft, compares GitHub's asset size and SHA-256 digest with the local file,
and publishes only after they match. If pre-publication verification fails,
the draft remains unpublished and the launcher ignores it.

Published releases are numbered `client-patches-vN`; an omitted number selects
the next unused release number. The launcher chooses the newest publication
containing the expected asset. It accepts both normal releases and
prereleases, so either choice distributes the DLL to players. Drafts do not.
The workflow's normal `GITHUB_TOKEN` cannot modify a different repository, so
it uses a GitHub App installed only on the public releases repository.

### Publish a DLL release

Every successful run distributes the resulting DLL to launcher users:

1. Commit and push the exact source intended for release to this private
   repository's default branch. The workflow refuses to publish from another
   branch.
2. Open the private repository's
   [Distribute release client patch DLL workflow][release-workflow].
3. Click **Run workflow** and leave the selected branch on the default branch.
4. Type `PUBLISH` exactly in the confirmation field.
5. Normally leave **release number** blank. The publisher selects the next
   unused `client-patches-vN`. Supply a positive number only when a specific
   unused number is required; never reuse an existing release or tag.
6. Choose whether GitHub labels the release as a prerelease.

   **Both normal releases and prereleases are live launcher distribution.**
   The prerelease setting changes GitHub's presentation only; it is not a test
   or staged channel.
7. Click **Run workflow**, open the new run, and wait for **Build, verify, and
   publish** to finish.
8. After a green run, open the
   [public client-patch releases][public-releases] and confirm that the new
   release is published, not a draft, and contains exactly:

   ```text
   Commonwealth-GA-Client-Patches-x86.dll
   ```

The job runs the registry test, builds with all runner CPU cores, creates a
short-lived App token scoped to the public repository, uploads the asset as a
draft, verifies its remote size and SHA-256 digest, and only then publishes
it. The public repository receives only the DLL and release metadata, never
this source tree.

If the workflow fails, inspect both published releases and drafts before
retrying. A retained draft is ignored by the launcher. A rare failure during
the final post-publication check can occur after the release became visible,
so a red workflow run alone does not prove that nothing was distributed.
Never replace an asset inside an already published release; fix the source and
publish a new numbered release.

[release-workflow]: https://github.com/Zotikus1001/commonwealth-ga-client-patch/actions/workflows/distribute-client-patch.yml
[public-releases]: https://github.com/Zotikus1001/commonwealth-ga-client-patches/releases

## Install and run

Place the built `dinput8.dll` beside `GlobalAgenda.exe`. Back up any existing
proxy DLL before replacing it.

Under Wine, configure `dinput8` as native before builtin. The output remains a
Win32 DLL; Wine loads it rather than treating it as a native Linux `.so`.

All normal and crash logs go to the `logs` directory beside
`GlobalAgenda.exe`. The patch never writes log files into the executable
directory itself. A DEBUG build uses this hierarchy:

```text
logs/
  process-<start-time>__pid<PID>/
    startup/
    instance-0001__<join-time>/
    instance-0002__<join-time>/
```

`startup` receives diagnostics before the first instance join. Each
`ReceivedPlayer` boundary then selects a fresh numbered instance child for
both normal channel files and crash reports. The process root prevents
concurrent clients from mixing output, while the children separate repeated
instance joins made by one running client. If directory rotation fails, the
previous directory remains active and the failure is logged there. Release
operational and crash logs remain directly under `logs`.

## Opt-in server feature gate

Future client features can require an exact server-advertised release. This is
a one-way activation gate, not a challenge/response authentication protocol:

```text
31          24 23          16 15                         0
+--------------+--------------+----------------------------+
| magic 0x6D   | feature ID   | exact feature release      |
+--------------+--------------+----------------------------+
```

- Feature ID is an assigned nonzero 8-bit value.
- Feature release is a nonzero 16-bit value.
- The server sends the token once through
  `APlayerController::ClientCapBandwidth(int)` after login or instance join,
  and only when that server feature is enabled.
- The client attempts activation only for a registered feature with the same
  ID and exact release. A failed activation callback leaves it inactive.
- While the handshake hook is attached, unknown IDs are consumed and ignored.
- A registered feature with a different release is disabled, including after
  an earlier matching advertisement, and produces one local Instance-channel
  blue message telling the player to update the client patch. Repeated
  mismatched tokens do not repeat the message.
- Client travel or a new `ReceivedPlayer` event deactivates negotiated
  features and resets mismatch notices. The next instance must advertise them
  again.
- There is no response, heartbeat, probe, or recurring packet.

DEBUG always attaches the handshake hook for its join diagnostics. Release
attaches it only when at least one server-gated feature is registered. A
release built with an empty registry therefore does not intercept feature
tokens. Before deploying an advertisement, ensure the supported client
population is running a release that installs the handshake hook.

In a debug build, the join summary waits two seconds after `ReceivedPlayer` so
the one-time advertisements can arrive. It reports each registered feature as
active, activation failed, release mismatched, or not advertised. “Not
advertised” means the client cannot distinguish a disabled server feature from
a server that does not provide it. This delay is local UI bookkeeping checked
from existing game-thread events; it sends no traffic and runs no timer thread.
If an advertisement arrives later, the debug build prints a corrected feature
status line. Repeated advertisements do not repeat that line unless the
feature state or advertised release changed.

`ClientCapBandwidth` is a reliable stock RPC. Send only after the new player
controller/session is ready; sending again after an instance transition is
required, while periodic resends within one instance are not.

### Client integration

Assign a stable ID and release, then register the feature in
`src/Handshake/ClientFeatureRegistry.cpp`:

```cpp
#include "src/Handshake/FeatureRegistry.hpp"

namespace {
constexpr ClientFeatureMagic::FeatureId kSpectatorId = 1;
constexpr ClientFeatureMagic::FeatureRelease kSpectatorRelease = 1;

bool SetSpectatorAvailable(bool active) {
	// Enable or disable only this feature's runtime state.
	return true;
}
}

bool RegisterClientFeatures() {
	return ClientFeatureHandshake::Register({
		kSpectatorId,
		kSpectatorRelease,
		"spectator",
		&SetSpectatorAvailable,
	});
}
```

Feature IDs and releases must be nonzero. Registration names must use static
storage and contain 1–64 printable ASCII characters. The fixed registry holds
at most 32 features, and `RegisterClientFeatures()` must return false if any
registration fails.

If the feature needs a hook, attach that hook in the normal startup Detours
transaction, but make its body pass straight through while
`ClientFeatureHandshake::IsActive(kSpectatorId)` is false. Do not make the
feature useful before activation.

### Server integration

Mirror or vendor `src/Handshake/FeatureMagic.hpp` into the server source and
send the matching token only when the server-side feature is enabled:

```cpp
constexpr std::uint8_t kSpectatorId = 1;
constexpr std::uint16_t kSpectatorRelease = 1;

playerController->ClientCapBandwidth(
	ClientFeatureMagic::MakeToken(kSpectatorId, kSpectatorRelease));
```

Send it after initial login and after each instance join. Never reuse a feature
ID for a different feature. Bump the feature release whenever the client and
server behavior or data contract becomes incompatible.

Because this gate is intentionally one-way, it proves only that a matching
server advertisement reached this client. It does not prove to the server that
the DLL is installed or that activation succeeded.

The mismatch message is inserted directly into the retail client's local chat
queue as channel 1. It uses the same blue Instance presentation and tab
filtering as a server `CHAT_MESSAGE`, but sends no chat or handshake packet.

Contributor rules and the same integration contract are recorded in
`AGENTS.md` and `CLAUDE.md`.
