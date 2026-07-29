# Repository Instructions

## Documentation contract

`AGENTS.md` and `CLAUDE.md` are mirrored instruction entry points. Keep them
byte-for-byte identical.

Use `README.md` for the current release inventory, build commands, installation
steps, and integration examples. Treat the implementation and tests as the
source of truth for mutable details. Do not copy change-prone facts into these
instruction files, including feature inventories, registered-feature state,
supported executable fingerprints, hook addresses, registry limits, protocol
bit masks, diagnostic delays, or output names.

When one of those details changes, update the defining code, its focused tests,
and the relevant README section together.

## Read before editing

Read `README.md`, the Makefile and build entry points, the DLL entry point, and
the complete subsystem being changed. Find every caller before changing a
shared layout, registry API, wire contract, hook lifecycle, or logging path.

Hard-coded addresses and ABI views are valid only for executables accepted by
the executable guard. Verify any address, field offset, calling convention, or
supported fingerprint against the target binary in Ghidra. Update the guard,
layout assertions, dependent hooks, tests, and README as one compatibility
change.

## Project organization

Keep code under the narrowest owning directory:

- `src/ClientPatches/<PatchName>/` owns a local-only client fix. Give each
  independent patch its own directory and keep its hook, private layouts,
  state, and patch-specific helpers there.
- `src/ClientFeatures/<FeatureName>/` owns the client half of behavior that
  requires a matching server advertisement. Create this directory only when a
  concrete gated feature is added; server implementation remains in the
  server repository.
- `src/Handshake/` owns only the reusable wire contract, registry, token
  interception, activation state, and registration composition root. Do not
  place feature behavior there or teach the transport about feature-specific
  rules.
- `src/ClientRuntime/` owns minimal, Ghidra-verified engine ABI views shared by
  more than one subsystem. Keep a layout used by only one patch private to that
  patch, and add compile-time size and offset assertions for every field the
  code relies on.
- `src/EntryPoint/` owns bootstrap and installation ordering, not patch
  behavior. `src/Proxy/` owns only DInput8 export forwarding.
- `src/Utils/` owns reusable infrastructure with no feature policy.
  `tests/` owns host-runnable focused tests, `data/` owns linker/export input,
  and `lib/` owns reviewed vendored code rather than project features.

Use PascalCase for patch and feature directories and keep public names aligned
with the owning directory. Include project headers from the repository root.
Do not add feature-specific headers to `src/pch.hpp`, create catch-all patch
files, or duplicate shared runtime and handshake helpers inside a feature.

## Adding client behavior

Classify behavior before implementing it. A fix that is safe and useful on any
supported server is a client patch. Behavior whose correctness, permissions,
or data contract depends on server cooperation is a server-gated client
feature; do not disguise it as a local patch to avoid negotiation.

For a local client patch:

1. Create `src/ClientPatches/<PatchName>/` and keep its implementation
   self-contained.
2. Add each translation unit explicitly to the Makefile source list.
3. Include its installer from the DLL entry point and attach it inside the
   existing startup Detours transaction. Return every attach failure so the
   transaction aborts atomically.
4. Keep the patch independent of the feature registry and server packets.
5. Update aggregate startup and DEBUG join diagnostics without moving the
   feature inventory into these instruction files.
6. Add focused tests for pure logic where it can run without the game, then
   perform the required clean builds and in-game check.

For a server-gated client feature:

1. Create `src/ClientFeatures/<FeatureName>/` for all feature-owned client
   behavior and state.
2. Add its single registration through
   `src/Handshake/ClientFeatureRegistry.cpp`; that file aggregates features but
   must not implement them.
3. Attach any required fixed-address hook in the normal atomic startup
   transaction, but make the hook pass through unchanged unless its feature ID
   is active. Every auxiliary entry point must enforce the same fail-closed
   boundary.
4. Keep activation and deactivation idempotent. Travel reset must remove all
   server-authorized runtime state, and activation failure must leave the
   feature unusable.
5. Add the client source to the Makefile, focused feature tests under
   `tests/`, and the matching server advertisement/integration change in the
   server repository. Update the README contract when integration steps or
   observable behavior change.

Do not start a nested Detours transaction inside a patch or feature installer.
Do not perform filesystem, hashing, registration, or hook work in `DllMain`.
If two patches need the same hook address, design one owner/dispatcher rather
than stacking order-dependent detours.

## Architecture and patch lifecycle

This repository builds a 32-bit DInput8 proxy DLL containing client fixes,
runtime diagnostics, and reusable opt-in server-feature gating infrastructure.
Keep server implementation out of this repository; document the server-side
integration contract in the README.

`DllMain` may only start the installation worker. Keep filesystem setup,
executable inspection, registration, and Detours work outside the loader lock.
Install startup hooks in one atomic transaction so an attach failure cannot
leave an unintended partial patch set.

Keep independent client fixes in focused patch directories. A local-only fix
must work without a cooperating server and must not register with the
server-feature gate. Limit ABI views to fields used by compiled code and guard
every relied-on size or offset.

Preserve DInput8 forwarding and the fail-closed executable guard. Unsupported
executables must continue without installing fixed-address hooks.

## Build and verification

Use the repository build entry points and Makefile rather than ad hoc compiler
commands. Keep Windows/MSYS2 and Linux cross-build support working. Build
scripts must derive their parallel job count from the processors available to
the current process; do not introduce fixed job counts.

After shared code changes, run the focused tests and clean DEBUG and release
builds documented in the README. Confirm developer-only code is present in
DEBUG where intended and absent from release. Use the Makefile’s declared
output and cleanup targets instead of duplicating paths in tooling.

## Logging and crash capture

Normal logs and crash reports must resolve under a `logs` directory beside the
game executable on Windows and Wine. Anchor paths to the process executable,
not the proxy DLL or working directory. Never fall back to writing files in the
game-directory root.

DEBUG builds must create one process root beneath `logs`, keep pre-join output
in its startup child, and rotate to a fresh child at each verified game-instance
join boundary. Normal logging and crash capture must consume the same
atomically published active directory so concurrent threads cannot observe a
partially changed path. Retain published path storage for fatal-handler safety.
Release builds keep their operational files directly beneath `logs`.

New diagnostics must use the configured logger directory instead of
constructing independent paths. Keep directory selection local and independent
of server-feature negotiation. When changing rotation behavior, verify startup,
repeated instance joins in one process, concurrent clients, rotation failure,
crashes before and after a join, and DEBUG code exclusion from release.

Keep release diagnostics limited to operational patch status, feature-gate
events needed by players, and crash capture. Put profiling and developer-only
join summaries behind the DEBUG build flag. Fatal-path logging must avoid
unsafe allocation and preserve the existing crash-handler constraints.

## Server-gated features

The gate is reusable infrastructure for future opt-in features. Do not freeze
it around the currently registered set. Each gated feature owns a stable
feature ID and compatibility release defined through the shared feature-magic
contract.

For each server-dependent feature:

1. Register it through the current client registry API and propagate
   registration failure so startup fails closed.
2. Keep every hook body and behavior inert until that feature’s runtime gate is
   active.
3. Use the shared token encoder and decoder; never duplicate the wire bit
   packing in client or server code.
4. Have the server advertise the feature only when its server implementation
   is enabled, after the player session or instance is ready.
5. Re-advertise after connection or instance boundaries that reset client
   state. Do not continuously poll or resend.
6. Never reuse a feature ID for unrelated behavior. Bump the compatibility
   release whenever client/server behavior or their data contract becomes
   incompatible.
7. Test matching, mismatched, unknown, duplicate, activation-failure, and reset
   paths.

Exact matches may attempt activation; an activation callback that fails must
leave the feature inert. Mismatches and conflicting later advertisements must
fail closed. Audit the handshake-hook installation policy before deploying a
new server advertisement so supported older client builds cannot pass reserved
tokens into the retail carrier function.

The current protocol is a one-way availability advertisement. It does not
authenticate either side, prove that the DLL is installed, or tell the server
whether activation succeeded. Do not bolt on a response, heartbeat, probe, or
periodic transport. If a future feature genuinely requires a stronger
protocol, design and version that change across client and server, then update
compatibility handling, tests, and documentation together.

## Local feature diagnostics

Keep player-facing mismatch notices and DEBUG summaries on the verified local
Instance-chat adapter. Preserve game-thread ownership of chat state and do not
send a chat or handshake packet merely to display a local notice.

Read the diagnostic implementation for its current states, timing, and
deduplication rules rather than restating them here. When behavior changes,
test travel, late advertisements, repeated advertisements, and unavailable
chat-manager paths. Keep developer-only summaries compiled out of release
unless the product contract is deliberately changed.

## Repository hygiene

Keep task memory, objects, binaries, maps, response files, runtime logs, and
other generated artifacts untracked. Never print or persist secrets. Preserve
unrelated user changes, keep feature work separated by concern, and record only
enduring non-obvious rationale in source comments.
