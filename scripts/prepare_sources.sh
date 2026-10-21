#!/bin/bash
# Place upstream sources in dep-src/ from versions.env. Skip a tree whose commit already matches.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck disable=SC1091
source "$ROOT/versions.env"
DEST="$ROOT/dep-src"
mkdir -p "$DEST"

sync_one() {
	local name="$1" url="$2" tag="$3"
	local dir="$DEST/$name"
	local current target

	if [[ -d "$dir/.git" ]]; then
		if ! git -C "$dir" rev-parse --verify --quiet "${tag}^{commit}" >/dev/null; then
			echo "fetch $name $tag"
			git -C "$dir" fetch --tags --force origin "refs/tags/${tag}:refs/tags/${tag}" ||
				git -C "$dir" fetch --tags origin
		fi
		current=$(git -C "$dir" rev-parse HEAD)
		target=$(git -C "$dir" rev-parse "${tag}^{commit}")
		if [[ "$current" == "$target" ]]; then
			echo "skip $name, already at $tag"
			return 0
		fi
		echo "checkout $name at $tag"
		git -C "$dir" checkout --detach "$tag"
		return 0
	fi

	echo "clone $name $tag"
	git clone --branch "$tag" --depth 1 "$url" "$dir"
}

sync_one optee_os "$OPTEE_OS_URL" "$OPTEE_OS_TAG"
sync_one optee_client "$OPTEE_CLIENT_URL" "$OPTEE_CLIENT_TAG"
sync_one optee_test "$OPTEE_TEST_URL" "$OPTEE_TEST_TAG"
sync_one optee_examples "$OPTEE_EXAMPLES_URL" "$OPTEE_EXAMPLES_TAG"
sync_one build "$OPTEE_BUILD_URL" "$OPTEE_BUILD_TAG"
sync_one trusted-firmware-a "$TFA_URL" "$TFA_TAG"
sync_one linux "$LINUX_URL" "$LINUX_TAG"
sync_one buildroot "$BUILDROOT_URL" "$BUILDROOT_TAG"
sync_one u-boot "$UBOOT_URL" "$UBOOT_TAG"
echo "dep-src is ready"
