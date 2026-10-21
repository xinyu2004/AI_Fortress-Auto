#!/bin/bash
# Linux console for one guest entry. SecOC and OTA share this UART, so only one may run.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN="$ROOT/dep-src/out/aarch64"
ENTRY="${1:-}"
LOCK=/tmp/fortress-aarch64-linux.lock
ENTRY_FILE=/tmp/fortress-aarch64-linux.entry
TEE_PORT=54321

case "$ENTRY" in
SecOC)
	guest_note="After Linux boots, perception and planning exchange a new plaintext every 100 ms."
	entry_name=secoc
	;;
OTA)
	guest_note="After Linux boots, ca-ota waits. In a third terminal run scripts/diag_tester.sh. KVN and min SVN use the REE filesystem, not RPMB."
	entry_name=ota
	;;
*)
	echo "usage: scripts/aarch64/run_SecOC.sh | scripts/aarch64/run_OTA.sh"
	exit 1
	;;
esac

missing=()
[[ -f "$BIN/bl1.bin" ]] || missing+=(bl1.bin)
[[ -f "$BIN/Image" ]] || missing+=(Image)
[[ -f "$BIN/rootfs.cpio.gz" ]] || missing+=(rootfs.cpio.gz)
if ((${#missing[@]})); then
	echo "Boot image is not ready. Missing: ${missing[*]}"
	echo "Build it with: scripts/aarch64/build_qemu.sh"
	exit 1
fi

FIP="$ROOT/dep-src/trusted-firmware-a/build/qemu/release/fip.bin"
for need in "$BIN/uImage" "$BIN/rootfs.cpio.uboot" "$FIP"; do
	if [[ ! -f "$need" ]]; then
		echo "Boot image is not ready. Missing: $need"
		echo "Build it with: scripts/aarch64/build_qemu.sh"
		exit 1
	fi
done

# BL1 reads the FIP at flash offset 0x40000. Pack bl1 at 0 and fip.bin there.
python3 - "$BIN/bl1.bin" "$FIP" "$BIN/flash.bin" <<'PY'
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

if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
	echo "qemu-system-aarch64 is not on PATH. scripts/aarch64/prepare_env.sh lists the package."
	exit 1
fi

exec 8>/tmp/fortress-qemu.lock
if ! flock -n 8; then
	echo "A QEMU guest is already running. Leave that window with Ctrl-a x."
	exit 1
fi
exec 9>"$LOCK"
if ! flock -n 9; then
	held=$(cat "$ENTRY_FILE" 2>/dev/null || echo "this Linux entry")
	echo "The Linux console is already held by ${held}."
	echo "SecOC and OTA are not run together. Leave that window with Ctrl-a x."
	exit 1
fi
echo "$ENTRY" >"$ENTRY_FILE"
printf 'fortress_entry=%s\n' "$entry_name" >"$BIN/fortress.entry"

echo "This window is the Linux console for ${ENTRY}."
echo "In another SSH session run: scripts/aarch64/run_tee.sh"
echo "Boot waits until that window is connected."
echo "${guest_note}"
echo "Leave this window with Ctrl-a x."

cd "$BIN"
# fd 9 stays open across exec, so the lock is held until QEMU exits.
NET=()
if [[ "$ENTRY" == "OTA" ]]; then
	OTA_PORT=$(sed -n 's/^#define FORTRESS_OTA_PORT \([0-9]*\)u/\1/p' "$ROOT/ta/common/wire.h")
	NET=(
		-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${OTA_PORT}-:${OTA_PORT}"
		-device virtio-net-pci,netdev=net0
	)
fi
exec qemu-system-aarch64 -display none -smp 2 -m 1057 \
	-cpu max,sme=on,pauth-impdef=on \
	-machine virt,acpi=off,secure=on,mte=off,gic-version=3,virtualization=off \
	-semihosting-config enable=on,target=native \
	-bios flash.bin -kernel Image -initrd rootfs.cpio.gz \
	-append 'console=ttyAMA0,38400 keep_bootcon root=/dev/vda2' \
	"${NET[@]}" \
	-serial mon:stdio \
	-serial tcp:127.0.0.1:${TEE_PORT},server=on,wait=on
