#!/bin/bash
# Compile only. If the environment or sources are missing, exit and name the script to run.
# Objects go to dep-src/out/aarch32 so an aarch64 build cannot replace them.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENV_FILE="$ROOT/dep-src/env-aarch32.sh"
OS_OUT="$ROOT/dep-src/out/aarch32/optee_os"
CLIENT_OUT="$ROOT/dep-src/out/aarch32/optee_client"

if [[ ! -f "$ENV_FILE" ]]; then
	echo "No aarch32 environment file. Run scripts/aarch32/prepare_env.sh"
	exit 1
fi
# shellcheck disable=SC1090
source "$ENV_FILE"

if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
	echo "${CROSS_COMPILE}gcc not found. Run scripts/aarch32/prepare_env.sh"
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
	CFG_ARM64_core=n \
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

echo "OP-TEE OS and optee_client are in dep-src/out/aarch32."
echo "Linux console: scripts/aarch32/run_SecOC.sh or scripts/aarch32/run_OTA.sh"
echo "QEMU: $QEMU_BIN -machine $QEMU_MACHINE"
