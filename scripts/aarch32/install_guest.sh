#!/bin/bash
# Build ta-ota, ta-soa, and the guest programs, then pack them into the AArch32 rootfs.
# Private keys stay under dep-src/out/aarch32/guest-fix. The ramdisk gets only public fixtures.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN="$ROOT/dep-src/out/aarch32"
FIX="$BIN/guest-fix"
KIT="$BIN/optee_os/export-ta_arm32"
CROSS="$ROOT/dep-src/toolchains/aarch32/bin/arm-linux-gnueabihf-"
FAKEROOT="$BIN/out-br/host/bin/fakeroot"
CPIO="$BIN/rootfs.cpio.gz"
HOST_GCC=$(ls "$BIN/out-br/host/bin/"arm-*-gcc 2>/dev/null | head -1 || true)

for need in "$KIT/mk/ta_dev_kit.mk" "$HOST_GCC" "$FAKEROOT" "$CPIO"; do
	if [[ ! -e "$need" ]]; then
		echo "Missing $need"
		echo "Build the image first: scripts/aarch32/build_qemu.sh"
		exit 1
	fi
done

make -C "$ROOT/protocol"
cc -Wall -Wextra -Werror -I"$ROOT/protocol/include" -o "$ROOT/rootfs_overlay/mkfixture" \
	"$ROOT/rootfs_overlay/mkfixture.c" "$ROOT/protocol/libfortress.a" -lcrypto
mkdir -p "$FIX"
"$ROOT/rootfs_overlay/mkfixture" "$FIX" "$ROOT/ta/ta-ota/trust_init.h"

make -C "$ROOT/ta/ta-soa" TA_DEV_KIT_DIR="$KIT" CROSS_COMPILE="$CROSS" clean
make -C "$ROOT/ta/ta-ota" TA_DEV_KIT_DIR="$KIT" CROSS_COMPILE="$CROSS" clean
make -C "$ROOT/ta/ta-soa" TA_DEV_KIT_DIR="$KIT" CROSS_COMPILE="$CROSS"
make -C "$ROOT/ta/ta-ota" TA_DEV_KIT_DIR="$KIT" CROSS_COMPILE="$CROSS"
PKCS11_TA=$(bash "$ROOT/scripts/rebuild_pkcs11_ta.sh" aarch32)

"$HOST_GCC" -Wall -Wextra -Werror \
	-I"$ROOT/ca" -I"$ROOT/ta/common" -I"$ROOT/protocol/include" \
	-o "$FIX/perception" "$ROOT/ca/perception.c" "$ROOT/ca/secoc_io.c" "$ROOT/ca/session.c" -lteec
"$HOST_GCC" -Wall -Wextra -Werror \
	-I"$ROOT/ca" -I"$ROOT/ta/common" -I"$ROOT/protocol/include" \
	-o "$FIX/planning" "$ROOT/ca/planning.c" "$ROOT/ca/secoc_io.c" "$ROOT/ca/session.c" -lteec
"$HOST_GCC" -Wall -Wextra -Werror \
	-I"$ROOT/ca" -I"$ROOT/ta/common" -I"$ROOT/protocol/include" \
	-o "$FIX/fortress-ota" "$ROOT/ca/fortress_ota.c" "$ROOT/ca/session.c" -lteec

WORK=$(mktemp -d)
cp -L "$CPIO" "$WORK/rootfs.cpio.gz"
"$FAKEROOT" -- bash -s "$WORK" "$ROOT" "$FIX" "$PKCS11_TA" <<'EOF'
set -euo pipefail
work="$1"
root="$2"
fix="$3"
pkcs11="$4"
unpack="$work/unpack"
mkdir -p "$unpack"
gzip -dc "$work/rootfs.cpio.gz" | (cd "$unpack" && cpio -idm)
rm -f "$unpack/usr/bin/fortress-secoc"
install -D -m 0755 "$fix/perception" "$unpack/usr/bin/perception"
install -D -m 0755 "$fix/planning" "$unpack/usr/bin/planning"
install -D -m 0755 "$fix/fortress-ota" "$unpack/usr/bin/fortress-ota"
install -D -m 0755 "$root/rootfs_overlay/S45fortress" "$unpack/etc/init.d/S45fortress"
cat >"$unpack/etc/init.d/S50udhcpc" <<'EOS'
#!/bin/sh
case "$1" in
  start)
	printf "Starting network (udhcpc): "
	if [ -d /sys/class/net/eth0 ]; then
		udhcpc -n -q -i eth0 >> /var/log/udhcpc.log 2>&1
		[ $? = 0 ] && echo "OK" || echo "FAIL"
	else
		echo "OK"
	fi
	;;
  stop)
	printf "Stopping network (udhcpc): "
	killall udhcpc
	[ $? = 0 ] && echo "OK" || echo "FAIL"
	;;
  restart|reload)
	"$0" stop
	"$0" start
	;;
  *)
	echo "Usage: $0 {start|stop|restart}"
	exit 1
	;;
esac
exit $?
EOS
chmod 0755 "$unpack/etc/init.d/S50udhcpc"
install -d -m 0755 "$unpack/etc/fortress"
install -m 0644 "$fix/manifest.bin" "$fix/image.hdr" "$fix/image_old.hdr" "$fix/payload.bin" \
	"$fix/soa.key" "$unpack/etc/fortress/"
install -m 0444 "$root/ta/ta-ota/"*.ta "$root/ta/ta-soa/"*.ta "$pkcs11" "$unpack/lib/optee_armtz/"
grep -q '^fortress-ota:' "$unpack/etc/group" || echo 'fortress-ota:x:2000:ca-ota' >>"$unpack/etc/group"
grep -q '^fortress-perception:' "$unpack/etc/group" || echo 'fortress-perception:x:2001:perception' >>"$unpack/etc/group"
grep -q '^fortress-planning:' "$unpack/etc/group" || echo 'fortress-planning:x:2002:planning' >>"$unpack/etc/group"
grep -q '^ca-ota:' "$unpack/etc/passwd" || echo 'ca-ota:x:2000:2000:OTA:/:/bin/sh' >>"$unpack/etc/passwd"
grep -q '^perception:' "$unpack/etc/passwd" || echo 'perception:x:2001:2001:perception:/:/bin/sh' >>"$unpack/etc/passwd"
grep -q '^planning:' "$unpack/etc/passwd" || echo 'planning:x:2002:2002:planning:/:/bin/sh' >>"$unpack/etc/passwd"
if grep -q '^teeclnt:' "$unpack/etc/group" && ! grep -q '^teeclnt:.*perception' "$unpack/etc/group"; then
	if grep -q '^teeclnt:.*ca-ota' "$unpack/etc/group"; then
		sed -i 's/^teeclnt:\([^:]*\):\([^:]*\):\([^:]*\)$/teeclnt:\1:\2:\3,perception,planning/' \
			"$unpack/etc/group"
	else
		sed -i 's/^teeclnt:\([^:]*\):\([^:]*\):\([^:]*\)$/teeclnt:\1:\2:\3,ca-ota,perception,planning/' \
			"$unpack/etc/group"
	fi
fi
rm -f "$work/new.cpio"
(cd "$unpack" && find . -print0 | cpio -0 -o -H newc >"$work/new.cpio")
gzip -9 -c "$work/new.cpio" >"$work/rootfs.cpio.gz"
EOF

cp -a "$WORK/rootfs.cpio.gz" "$CPIO"
if [[ -L "$CPIO" ]]; then
	cp -a "$WORK/rootfs.cpio.gz" "$(readlink -f "$CPIO")"
fi
rm -rf "$WORK"
echo "Guest programs are in the AArch32 rootfs."
echo "Private signing keys stay in dep-src/out/aarch32/guest-fix."
