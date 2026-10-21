#!/bin/bash
# Compile only. If the environment or sources are missing, exit and name the script to run.
# Objects go to dep-src/out/aarch64 so an aarch32 build cannot replace them.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENV_FILE="$ROOT/dep-src/env-aarch64.sh"
OS_OUT="$ROOT/dep-src/out/aarch64/optee_os"
CLIENT_OUT="$ROOT/dep-src/out/aarch64/optee_client"

if [[ ! -f "$ENV_FILE" ]]; then
	echo "No aarch64 environment file. Run scripts/aarch64/prepare_env.sh"
	exit 1
fi
# shellcheck disable=SC1090
source "$ENV_FILE"

if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
	echo "${CROSS_COMPILE}gcc not found. Run scripts/aarch64/prepare_env.sh"
	exit 1
fi
if [[ ! -d "$ROOT/dep-src/optee_os/.git" ]]; then
	echo "dep-src/optee_os is missing. Run scripts/prepare_sources.sh"
	exit 1
fi

make -C "$ROOT/protocol" test
make -C "$ROOT/boot"

echo "Build OP-TEE OS ($OPTEE_PLATFORM) -> $OS_OUT"
make -C "$ROOT/dep-src/optee_os" \
	O="$OS_OUT" \
	PLATFORM="$OPTEE_PLATFORM" \
	CROSS_COMPILE="$CROSS_COMPILE" \
	CROSS_COMPILE_ta_arm64="$CROSS_COMPILE" \
	CFG_ARM64_core=y \
	CFG_USER_TA_TARGETS=ta_arm64 \
	-j"$(nproc)"

if [[ -d "$ROOT/dep-src/optee_client" ]]; then
	"$ROOT/scripts/build_libuuid.sh" "$CROSS_COMPILE"
	uuid_pc="$ROOT/dep-src/sysroot/${CROSS_COMPILE%-}/lib/pkgconfig"
	echo "Build optee_client -> $CLIENT_OUT"
	PKG_CONFIG=pkg-config PKG_CONFIG_LIBDIR="$uuid_pc" \
		make -C "$ROOT/dep-src/optee_client" \
		O="$CLIENT_OUT" \
		CROSS_COMPILE="$CROSS_COMPILE" \
		WITH_TEEACL=1 \
		-j"$(nproc)"
fi

echo "OP-TEE OS and optee_client are in dep-src/out/aarch64."
echo "The QEMU image is scripts/aarch64/build_qemu.sh. See docs/en/build-qemu.md."
echo "Linux console: scripts/aarch64/run_SecOC.sh or scripts/aarch64/run_OTA.sh"
echo "QEMU: $QEMU_BIN -machine $QEMU_MACHINE"
