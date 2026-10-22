#!/bin/bash
# Linux console for one AArch32 guest entry. SecOC and OTA share this UART, so only one may run.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN="$ROOT/dep-src/out/aarch32"
ENTRY="${1:-}"
BOOT="${2:-}"
LOCK=/tmp/fortress-aarch32-linux.lock
ENTRY_FILE=/tmp/fortress-aarch32-linux.entry
TEE_PORT=54321

case "$ENTRY" in
SecOC)
	guest_note="After Linux boots, perception and planning exchange a new plaintext every 100 ms."
	entry_name=secoc
	;;
OTA)
	guest_note="After Linux boots, ca-ota waits. In a third terminal run scripts/diag_tester.sh aarch32. KVN and min SVN use the REE filesystem, not RPMB."
	entry_name=ota
	;;
*)
	echo "usage: scripts/aarch32/run_SecOC.sh [secure] | scripts/aarch32/run_OTA.sh [secure]"
	exit 1
	;;
esac
if [[ -n "$BOOT" && "$BOOT" != "secure" ]]; then
	echo "usage: scripts/aarch32/run_SecOC.sh [secure] | scripts/aarch32/run_OTA.sh [secure]"
	exit 1
fi

missing=()
[[ -f "$BIN/bl1.bin" ]] || missing+=(bl1.bin)
[[ -f "$BIN/zImage" ]] || missing+=(zImage)
[[ -f "$BIN/rootfs.cpio.gz" ]] || missing+=(rootfs.cpio.gz)
if ((${#missing[@]})); then
	echo "Boot image is not ready. Missing: ${missing[*]}"
	echo "Build it with: scripts/aarch32/build_qemu.sh"
	exit 1
fi

BL1=$(readlink -f "$BIN/bl1.bin")
FIP="$(dirname "$BL1")/fip.bin"
FLASH="$BIN/flash.bin"
if [[ "$BOOT" == "secure" ]]; then
	BL1="$BIN/secure/bl1.bin"
	FIP="$BIN/secure/fip.bin"
	FLASH="$BIN/secure/flash.bin"
fi
if [[ ! -f "$BIN/fit.itb" || ! -f "$BL1" || ! -f "$FIP" ]]; then
	echo "Boot image is not ready. Missing: $BIN/fit.itb or $BL1 or $FIP"
	if [[ "$BOOT" == "secure" ]]; then
		echo "Build it with: scripts/aarch32/build_qemu.sh secure"
	else
		echo "Build it with: scripts/aarch32/build_qemu.sh"
	fi
	exit 1
fi

python3 - "$BL1" "$FIP" "$FLASH" <<'PY'
import pathlib, sys
bl1 = pathlib.Path(sys.argv[1]).read_bytes()
fip = pathlib.Path(sys.argv[2]).read_bytes()
base = 0x40000
if len(bl1) > base:
    raise SystemExit("bl1.bin overlaps the FIP offset")
need = base + len(fip)
size = 1
while size < need:
    size *= 2
image = bytearray(b"\xff" * size)
image[:len(bl1)] = bl1
image[base:base + len(fip)] = fip
pathlib.Path(sys.argv[3]).write_bytes(image)
PY

if ! command -v qemu-system-arm >/dev/null 2>&1; then
	echo "qemu-system-arm is not on PATH. scripts/aarch32/prepare_env.sh lists the package."
	exit 1
fi

exec 8>/tmp/fortress-qemu.lock
if ! flock -n 8; then
	echo "A QEMU guest is already running. Leave that window with Ctrl-a x."
	exit 1
fi
exec 9>"$LOCK"
if ! flock -n 9; then
	held=$(cat "$ENTRY_FILE" 2>/dev/null || echo "the other Linux entry")
	echo "The Linux console is already held by ${held}."
	echo "SecOC and OTA are not run together. Leave that window with Ctrl-a x."
	exit 1
fi
echo "$ENTRY" >"$ENTRY_FILE"
printf 'fortress_entry=%s\n' "$entry_name" >"$BIN/fortress.entry"

echo "This window is the Linux console for AArch32 ${ENTRY}."
echo "In another SSH session run: scripts/aarch32/run_tee.sh"
echo "Boot waits until that window is connected."
echo "${guest_note}"
echo "Leave this window with Ctrl-a x."

cd "$BIN"
# The NIC is always present so the signed device tree matches SecOC and OTA.
OTA_PORT=$(sed -n 's/^#define FORTRESS_OTA_PORT \([0-9]*\)u/\1/p' "$ROOT/ta/common/wire.h")
NET=(
	-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${OTA_PORT}-:${OTA_PORT}"
	-device virtio-net-device,netdev=net0
)
exec qemu-system-arm -display none -smp 2 -m 1057 \
	-cpu cortex-a15 \
	-machine virt,secure=on \
	-semihosting-config enable=on,target=native \
	-bios "$FLASH" \
	"${NET[@]}" \
	-serial mon:stdio \
	-serial tcp:127.0.0.1:${TEE_PORT},server=on,wait=on
