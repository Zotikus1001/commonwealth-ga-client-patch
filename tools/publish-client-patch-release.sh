#!/usr/bin/env bash
set -euo pipefail

# Publishes only the already packaged release asset. Build before obtaining a
# release credential so compiler and test processes never inherit that token.
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
release_repository="Zotikus1001/commonwealth-ga-client-patches"
asset_name="Commonwealth-GA-Client-Patches-x86.dll"
asset_path="$repo_dir/out/distribution/$asset_name"
release_number=""
release_kind=""

usage() {
	cat <<'EOF'
Usage: bash tools/publish-client-patch-release.sh [options]

Options:
  --release-number N  Publish client-patches-vN; default is the next number.
  --prerelease       Mark the GitHub release as a prerelease.
  --stable           Mark the GitHub release as a normal release.
  -h, --help         Show this help.
EOF
}

fail() {
	echo "Error: $*" >&2
	exit 1
}

while (($# > 0)); do
	case "$1" in
		--release-number)
			(($# >= 2)) || fail "--release-number requires a value"
			release_number="$2"
			shift 2
			;;
		--prerelease)
			[[ -z "$release_kind" ]] || fail "choose either --prerelease or --stable"
			release_kind="prerelease"
			shift
			;;
		--stable)
			[[ -z "$release_kind" ]] || fail "choose either --prerelease or --stable"
			release_kind="stable"
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			fail "unknown option: $1"
			;;
	esac
done

if [[ -n "$release_number" && ! "$release_number" =~ ^[1-9][0-9]*$ ]]; then
	fail "release number must be a positive integer"
fi
if [[ -n "$release_number" ]]; then
	((${#release_number} <= 10)) || fail "release number is too large"
	((10#$release_number <= 2147483647)) || fail "release number is too large"
fi
[[ -n "$release_kind" ]] || fail "choose either --prerelease or --stable"
[[ "${GITHUB_ACTIONS:-}" == "true" ]] ||
	fail "release publishing is restricted to GitHub Actions"

for tool in gh i686-w64-mingw32-objdump sha256sum grep; do
	command -v "$tool" >/dev/null 2>&1 || fail "missing required tool: $tool"
done
[[ -f "$asset_path" ]] || fail "packaged release asset not found: $asset_path"
[[ ! -e "$repo_dir/out/clientpatch/dinput8.dll.map" ]] ||
	fail "debug linker map is present; run a clean package-release build"

file_header="$(i686-w64-mingw32-objdump -f "$asset_path")" ||
	fail "could not inspect the packaged DLL"
grep -q "file format pei-i386" <<<"$file_header" ||
	fail "packaged asset is not a 32-bit x86 PE file"

section_headers="$(i686-w64-mingw32-objdump -h "$asset_path")" ||
	fail "could not inspect DLL sections"
if grep -Eq '(^|[[:space:]])\.debug_' <<<"$section_headers"; then
	fail "packaged asset contains debug sections"
fi

hash_line="$(sha256sum -- "$asset_path")"
asset_sha256="${hash_line%% *}"
asset_size="$(wc -c <"$asset_path")"
asset_size="${asset_size//[[:space:]]/}"
[[ "$asset_sha256" =~ ^[a-f0-9]{64}$ ]] || fail "could not calculate DLL SHA-256"
[[ "$asset_size" =~ ^[1-9][0-9]*$ ]] || fail "could not calculate DLL size"

repository_push="$(
	gh api "repos/$release_repository" --jq '.permissions.push // false'
)" || fail "could not query the public release repository"
[[ "$repository_push" == "true" ]] ||
	fail "GitHub credential has no write permission for $release_repository"

release_tags="$(
	gh release list \
		--repo "$release_repository" \
		--limit 1000 \
		--json tagName \
		--jq '.[].tagName'
)" || fail "could not list existing releases"

if [[ -z "$release_number" ]]; then
	max_release=0
	while IFS= read -r tag; do
		if [[ "$tag" =~ ^client-patches-v([1-9][0-9]*)$ ]]; then
			number="${BASH_REMATCH[1]}"
			if ((10#$number > max_release)); then
				max_release=$((10#$number))
			fi
		fi
	done <<<"$release_tags"
	((max_release < 2147483647)) || fail "release number limit reached"
	release_number=$((max_release + 1))
fi

release_tag="client-patches-v$release_number"
release_title="Client Patches v$release_number"
if gh release view "$release_tag" --repo "$release_repository" >/dev/null 2>&1; then
	fail "release already exists: $release_tag"
fi
if gh api "repos/$release_repository/git/ref/tags/$release_tag" --silent >/dev/null 2>&1; then
	fail "tag already exists without a release: $release_tag"
fi

echo
echo "Public repository: $release_repository"
echo "Release tag:      $release_tag"
echo "Release type:     $release_kind"
echo "Asset:            $asset_name"
echo "Size:             $asset_size bytes"
echo "SHA-256:          $asset_sha256"
echo

draft_created=false
release_published=false
report_unpublished_draft() {
	status=$?
	if ((status != 0)) && [[ "$draft_created" == true && "$release_published" == false ]]; then
		echo "The verified publication did not complete. Draft $release_tag was retained." >&2
	fi
}
trap report_unpublished_draft EXIT

create_args=(
	release create "$release_tag" "$asset_path"
	--repo "$release_repository"
	--title "$release_title"
	--notes "Launcher-managed client patch release."
	--draft
)
if [[ "$release_kind" == "prerelease" ]]; then
	create_args+=(--prerelease)
fi

gh "${create_args[@]}" >/dev/null
draft_created=true

release_id="$(
	gh release view "$release_tag" \
		--repo "$release_repository" \
		--json databaseId \
		--jq '.databaseId'
)" || fail "could not resolve the draft release ID"
[[ "$release_id" =~ ^[1-9][0-9]*$ ]] || fail "GitHub returned an invalid release ID"

release_state="$(
	gh api "repos/$release_repository/releases/$release_id" \
		--jq '[.draft, .prerelease] | @tsv'
)" || fail "could not inspect the draft release"
IFS=$'\t' read -r remote_draft remote_prerelease <<<"$release_state"
[[ "$remote_draft" == "true" ]] || fail "GitHub release was not retained as a draft"
if [[ "$release_kind" == "prerelease" ]]; then
	[[ "$remote_prerelease" == "true" ]] || fail "GitHub lost the prerelease flag"
else
	[[ "$remote_prerelease" == "false" ]] || fail "GitHub added an unexpected prerelease flag"
fi

remote_asset="$(
	gh api "repos/$release_repository/releases/$release_id" \
		--jq ".assets[] | select(.name == \"$asset_name\") | [.name, (.size | tostring), (.digest // \"\")] | @tsv"
)" || fail "could not inspect the draft release asset"
[[ -n "$remote_asset" ]] || fail "draft release is missing $asset_name"
IFS=$'\t' read -r remote_name remote_size remote_digest <<<"$remote_asset"
[[ "$remote_name" == "$asset_name" ]] || fail "GitHub returned the wrong asset name"
[[ "$remote_size" == "$asset_size" ]] || fail "GitHub asset size does not match the local DLL"
[[ "$remote_digest" == "sha256:$asset_sha256" ]] ||
	fail "GitHub asset SHA-256 does not match the local DLL"

gh release edit "$release_tag" \
	--repo "$release_repository" \
	--draft=false >/dev/null
release_published=true

published_state="$(
	gh release view "$release_tag" \
		--repo "$release_repository" \
		--json isDraft,isPrerelease,url \
		--jq '[.isDraft, .isPrerelease, .url] | @tsv'
)" || fail "release published, but final verification failed"
IFS=$'\t' read -r published_draft published_prerelease published_url <<<"$published_state"
[[ "$published_draft" == "false" ]] || fail "release is unexpectedly still a draft"
if [[ "$release_kind" == "prerelease" ]]; then
	[[ "$published_prerelease" == "true" ]] || fail "published prerelease flag is missing"
else
	[[ "$published_prerelease" == "false" ]] || fail "published release is unexpectedly a prerelease"
fi

echo "Published and verified: $published_url"
