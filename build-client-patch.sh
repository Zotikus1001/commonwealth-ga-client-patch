#!/usr/bin/env bash
set -euo pipefail

# Linux and MSYS2 share this entry point so both use the processor count
# available to the current process rather than a fixed parallel-job value.
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mode="${1:-}"

case "$mode" in
	debug|release|package-release)
		;;
	*)
		echo "Usage: bash build-client-patch.sh debug|release|package-release" >&2
		exit 2
		;;
esac

for tool in make i686-w64-mingw32-g++; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "Missing required build tool: $tool" >&2
		exit 1
	fi
done

jobs=""
if command -v nproc >/dev/null 2>&1 &&
	jobs="$(nproc 2>/dev/null)"; then
	:
elif command -v getconf >/dev/null 2>&1 &&
	jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null)"; then
	:
else
	jobs=""
fi

case "$jobs" in
	""|0|*[!0-9]*)
		echo "Could not determine the number of available processors." >&2
		exit 1
		;;
esac

echo "Using $jobs parallel build jobs."
make -C "$repo_dir" -j"$jobs" "$mode"
if [[ "$mode" == "package-release" ]]; then
	echo "Packaged: $repo_dir/out/distribution/Commonwealth-GA-Client-Patches-x86.dll"
else
	echo "Built: $repo_dir/out/clientpatch/dinput8.dll"
fi
