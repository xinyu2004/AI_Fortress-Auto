#!/bin/bash
# Apply the session allowlist and rebuild only the PKCS#11 TA.
# A full OP-TEE make without PLATFORM=vexpress-qemu_armv8a rewrites conf.mk.
# This script compiles the two patched files and relinks the TA.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ARCH="${1:-}"
SRC="$ROOT/dep-src/optee_os"

if [[ "$ARCH" != "aarch64" && "$ARCH" != "aarch32" ]]; then
	echo "usage: scripts/rebuild_pkcs11_ta.sh aarch64|aarch32" >&2
	exit 1
fi

python3 "$ROOT/scripts/patch_pkcs11_ta.py"

if [[ "$ARCH" == "aarch64" ]]; then
	out="$SRC/out/arm"
	cross="$ROOT/dep-src/toolchains/aarch64/bin/aarch64-linux-gnu-"
	kit="$out/export-ta_arm64"
	# The saved header is the AArch64 configuration. A mistaken make replaced
	# out/arm/conf.mk with the 32-bit defaults. Put the saved one back.
	cp "$kit/host_include/conf.mk" "$out/conf.mk"
	cp "$kit/host_include/conf.h" "$out/include/generated/conf.h"
else
	out="$ROOT/dep-src/out/aarch32/optee_os"
	cross="$ROOT/dep-src/toolchains/aarch32/bin/arm-linux-gnueabihf-"
	kit="$out/export-ta_arm32"
fi

ta_dir="$out/ta/pkcs11"
uuid="fd02c9da-306c-48c7-a49c-bbd827ae86ee"
ta="$ta_dir/$uuid.ta"

rebuild_obj() {
	local cmdfile="$1"
	local cmd
	cmd=$(sed -n 's/^old-cmd-[^ ]* := //p' "$cmdfile")
	if [[ -z "$cmd" ]]; then
		echo "No compile command in $cmdfile" >&2
		exit 1
	fi
	(cd "$SRC" && eval "$cmd")
}

rebuild_obj "$ta_dir/src/.entry.o.cmd"
rebuild_obj "$ta_dir/src/.pkcs11_token.o.cmd"

objs=()
while IFS= read -r obj; do
	objs+=("$obj")
done < <(find "$ta_dir" -name '*.o' | sort)

libgcc=$("${cross}gcc" -print-libgcc-file-name)
map="$ta_dir/$uuid.map"
elf="$ta_dir/$uuid.elf"
"${cross}ld" -e__ta_entry -pie -T "$ta_dir/ta.lds" -Map="$map" \
	--sort-section=alignment -z max-page-size=4096 --as-needed -static \
	--dynamic-list "$ta_dir/dyn_list" \
	"${objs[@]}" \
	-L "$kit/lib" --start-group -lutils -lutee -lmbedtls --end-group \
	"$libgcc" -o "$elf"
"${cross}objcopy" --strip-unneeded "$elf" "$ta_dir/$uuid.stripped.elf"
python3 "$kit/scripts/sign_encrypt.py" --key "$kit/keys/default_ta.pem" \
	--uuid "$uuid" --ta-version 0 \
	--in "$ta_dir/$uuid.stripped.elf" --out "$ta"
mkdir -p "$kit/ta"
cp -a "$ta" "$kit/ta/"
echo "$ta"
