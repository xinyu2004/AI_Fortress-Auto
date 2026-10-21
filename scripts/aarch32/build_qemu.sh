#!/bin/bash
# Build the AArch32 QEMU image. Does not download sources or install packages.
# Uses OP-TEE 4.10.0 build/qemu.mk. Does not build QEMU itself. fTPM and Rust stay off.
# Boot files go to dep-src/out/aarch32. Buildroot's work tree is dep-src/out/aarch32/out-br.
# Linux, U-Boot, OP-TEE, and TF-A each get their own output directory so this
# build does not replace the AArch64 image.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$ROOT/dep-src"
OUT="$SRC/out/aarch32"
OS_OUT="$OUT/optee_os"

need() {
	if [[ ! -d "$SRC/$1/.git" ]]; then
		echo "dep-src/$1 is missing. Run scripts/prepare_sources.sh"
		exit 1
	fi
}

need build
need optee_os
need optee_client
need optee_test
need optee_examples
need trusted-firmware-a
need linux
need buildroot
need u-boot

host_missing=()
command -v bison >/dev/null 2>&1 || host_missing+=(bison)
command -v flex >/dev/null 2>&1 || host_missing+=(flex)
command -v dtc >/dev/null 2>&1 || host_missing+=(dtc)
if ((${#host_missing[@]})); then
	echo "missing host tools: ${host_missing[*]}"
	echo "Run scripts/aarch32/prepare_env.sh"
	exit 1
fi

# qemu.mk writes TF-A and U-Boot into the shared source trees. Point those
# outputs aside. Re-apply after a fresh checkout of dep-src/build.
python3 - "$SRC/build/qemu.mk" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
text = text.replace(
    "TF_A_OUT = $(TF_A_PATH)/build/qemu/release",
    "TF_A_BUILD ?= $(TF_A_PATH)/build/qemu\nTF_A_OUT = $(TF_A_BUILD)/release",
)
text = text.replace(
    "TF_A_OUT = $(TF_A_PATH)/build/qemu/debug",
    "TF_A_OUT = $(TF_A_BUILD)/debug",
)
text = text.replace("BL33=$(ROOT)/u-boot/u-boot.bin", "BL33=$(U-BOOT_PATH)/u-boot.bin")
text = text.replace("ln -sf $(ROOT)/u-boot/u-boot.bin", "ln -sf $(U-BOOT_PATH)/u-boot.bin")
if "BUILD_PLAT=$(TF_A_OUT)" not in text:
    needle = "\tPLAT=qemu \\\n"
    if needle not in text:
        raise SystemExit("qemu.mk: PLAT=qemu line not found")
    text = text.replace(needle, needle + "\tBUILD_PLAT=$(TF_A_OUT) \\\n", 1)
path.write_text(text)
PY

worktree() {
	local repo="$1" dest="$2"
	if [[ -d "$dest/.git" || -f "$dest/.git" ]]; then
		return 0
	fi
	git -C "$repo" worktree add --detach "$dest" HEAD
}

worktree "$SRC/linux" "$SRC/linux-arm"
worktree "$SRC/u-boot" "$SRC/u-boot-arm"
mkdir -p "$OUT" "$OS_OUT"

# glibc 2.43 defines memchr, bsearch, and wmemchr as _Generic macros.
# m4 1.4.20's gnulib redeclares those names. Undef the macros first.
fix_gnulib_generic() {
	local build="$OUT/out-br/build"
	[[ -d "$build" ]] || return 0
	python3 - "$build" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
repls = (
    ("# ifndef __cplusplus\n_GL_EXTERN_C void *memchr (",
     "# ifndef __cplusplus\n# undef memchr\n_GL_EXTERN_C void *memchr ("),
    ("# ifndef __cplusplus\n_GL_EXTERN_C wchar_t *wmemchr (",
     "# ifndef __cplusplus\n# undef wmemchr\n_GL_EXTERN_C wchar_t *wmemchr ("),
    ("#if defined __GNUC__ && __GNUC__ >= 15 && !defined __clang__\n_GL_EXTERN_C void *bsearch (",
     "#if defined __GNUC__ && __GNUC__ >= 15 && !defined __clang__\n# undef bsearch\n_GL_EXTERN_C void *bsearch ("),
)
names = {"string.h", "string.in.h", "stdlib.h", "stdlib.in.h", "wchar.h", "wchar.in.h"}
changed = 0
for path in root.rglob("*"):
    if path.name not in names or not path.is_file():
        continue
    text = path.read_text(errors="replace")
    new = text
    for old, repl in repls:
        if old in new:
            new = new.replace(old, repl)
    if new != text:
        path.write_text(new)
        changed += 1
print(f"gnulib headers adjusted: {changed}")
PY
}

MAKE_FLAGS=(
	-C "$SRC/build"
	-f qemu.mk
	BINARIES_PATH="$OUT"
	BR_OUT="$OUT/out-br"
	LINUX_PATH="$SRC/linux-arm"
	U-BOOT_PATH="$SRC/u-boot-arm"
	TF_A_BUILD="$OUT/tf-a"
	OPTEE_OS_COMMON_EXTRA_FLAGS="O=$OS_OUT CFG_ENABLE_EMBEDDED_TESTS=n"
	OPTEE_OS_BIN="$OS_OUT/core/tee.bin"
	OPTEE_OS_HEADER_V2_BIN="$OS_OUT/core/tee-header_v2.bin"
	OPTEE_OS_PAGER_V2_BIN="$OS_OUT/core/tee-pager_v2.bin"
	OPTEE_OS_PAGEABLE_V2_BIN="$OS_OUT/core/tee-pageable_v2.bin"
	OPTEE_OS_TA_DEV_KIT_DIR="$OS_OUT/export-ta_arm32"
	MEASURED_BOOT_FTPM=n
	RUST_ENABLE=n
	CFG_TEE_CORE_LOG_LEVEL=2
)

unset CARGO_TARGET_DIR

echo "Download the ARM GNU toolchain used by OP-TEE 4.10.0 into dep-src/toolchains"
make "${MAKE_FLAGS[@]}" aarch32-toolchain

# Host mkeficapsule needs gnutls. This image does not use capsules.
# The boot command also imports fortress.entry so one image can start SecOC or OTA.
uboot_frag="$SRC/build/kconfigs/u-boot_qemu_virt_v7.conf"
python3 - "$uboot_frag" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
if "CONFIG_TOOLS_MKEFICAPSULE" not in text:
    text += "\n# Host mkeficapsule needs gnutls. This image does not use capsules.\n# CONFIG_TOOLS_MKEFICAPSULE is not set\n"
old = "load hostfs - ${ramdisk_addr_r} rootfs.cpio.gz && setenv bootargs console=ttyAMA0,115200 earlyprintk=serial,ttyAMA0,115200 && fdt chosen ${ramdisk_addr_r} ${filesize} && bootz ${kernel_addr_r} - ${fdt_addr}"
new = "load hostfs - ${ramdisk_addr_r} rootfs.cpio.gz && setenv rd_size ${filesize} && load hostfs - 0x40200000 fortress.entry && env import -t 0x40200000 ${filesize} && setenv bootargs console=ttyAMA0,115200 earlyprintk=serial,ttyAMA0,115200 fortress.entry=${fortress_entry} && fdt chosen ${ramdisk_addr_r} ${rd_size} && bootz ${kernel_addr_r} - ${fdt_addr}"
if "fortress.entry" not in text:
    if old not in text:
        raise SystemExit("u-boot_qemu_virt_v7.conf boot command no longer matches")
    text = text.replace(old, new, 1)
path.write_text(text)
PY

echo "Build Linux, Buildroot, U-Boot, OP-TEE, and TF-A"
rc=1
for _try in 1 2 3; do
	set +e
	make "${MAKE_FLAGS[@]}" -j"$(nproc)" linux buildroot arm-tf
	rc=$?
	set -e
	[[ "$rc" -eq 0 ]] && break
	fix_gnulib_generic
done
if [[ "$rc" -ne 0 ]]; then
	exit "$rc"
fi

ln -sfn "$OUT/out-br/images/rootfs.cpio.gz" "$OUT/rootfs.cpio.gz"
"$ROOT/scripts/aarch32/install_guest.sh"

echo "Image is in dep-src/out/aarch32 (bl1.bin, zImage, rootfs.cpio.gz)."
echo "TF-A Trusted Board Boot stays off, so this image does not check the boot public key."
echo "Linux console, one at a time: scripts/aarch32/run_SecOC.sh or scripts/aarch32/run_OTA.sh"
echo "OP-TEE console, in another terminal: scripts/aarch32/run_tee.sh"
