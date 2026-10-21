#!/bin/bash
# Build the AArch64 QEMU image. Does not download sources or install packages.
# Uses OP-TEE 4.10.0 build/qemu_v8.mk. Does not build QEMU itself. fTPM and Rust stay off.
# Boot files go to dep-src/out/aarch64. Buildroot's work tree is dep-src/out/aarch64/out-br.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$ROOT/dep-src"

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
	echo "Run scripts/aarch64/prepare_env.sh"
	exit 1
fi

# qemu_arm64 turns on the host tool mkeficapsule, which includes gnutls.h.
# This image does not build EFI capsules, so keep that tool off.
uboot_frag="$SRC/build/kconfigs/u-boot_qemu_v8.conf"
if ! grep -q 'CONFIG_TOOLS_MKEFICAPSULE' "$uboot_frag"; then
	printf '\n# Host mkeficapsule needs gnutls. This image does not use capsules.\n# CONFIG_TOOLS_MKEFICAPSULE is not set\n' >>"$uboot_frag"
fi
if [[ -f "$SRC/u-boot/.config" ]] && grep -q '^CONFIG_TOOLS_MKEFICAPSULE=y' "$SRC/u-boot/.config"; then
	touch "$uboot_frag"
fi

# Upstream common.mk defaults BR_OUT to $(ROOT)/out-br. This image keeps that
# tree under the arch output. Re-apply after a fresh checkout of dep-src/build.
pin_br_out() {
	python3 - "$SRC/build/common.mk" "$SRC/build/qemu_v8.mk" <<'PY'
import pathlib, sys
common = pathlib.Path(sys.argv[1])
qemu = pathlib.Path(sys.argv[2])
text = common.read_text()
if "BR_OUT" not in text:
    lines = []
    found = False
    for line in text.splitlines(keepends=True):
        if line.startswith("BUILDROOT_TARGET_ROOT") and "$(ROOT)/out-br/target" in line:
            lines.append("BR_OUT\t\t\t\t?= $(ROOT)/out-br\n")
            lines.append(line.replace("$(ROOT)/out-br", "$(BR_OUT)"))
            found = True
            continue
        line = line.replace("../out-br", "$(BR_OUT)")
        line = line.replace("--out out-br ", "--out $(BR_OUT) ")
        line = line.replace("--br-defconfig out-br/extra.conf", "--br-defconfig $(BR_OUT)/extra.conf")
        line = line.replace("$(ROOT)/out-br", "$(BR_OUT)")
        lines.append(line)
    if not found:
        raise SystemExit("common.mk: BUILDROOT_TARGET_ROOT line not found")
    text = "".join(lines)
    if "$(ROOT)/out-br" not in text.split("BR_OUT", 1)[1]:
        raise SystemExit("common.mk: BR_OUT default was rewritten")
    common.write_text(text)
qtext = qemu.read_text().replace("$(ROOT)/out-br", "$(BR_OUT)")
if qtext != qemu.read_text():
    qemu.write_text(qtext)
PY
}

# Upstream used to write dep-src/out-br. This image uses dep-src/out/aarch64/out-br.
# Remove a leftover symlink or directory at the old names.
place_out_br() {
	local dest="$SRC/out/aarch64/out-br"
	local old_named="$SRC/out/aarch64/buildroot"
	mkdir -p "$SRC/out/aarch64"
	rewrite_tree() {
		python3 - "$1" "$2" <<'PY'
import pathlib, subprocess, sys
old, dest = sys.argv[1], sys.argv[2]
old_b, new_b = old.encode(), dest.encode()
listed = subprocess.run(
    ["rg", "-l", "-F", "--hidden", "--no-ignore", old, dest],
    capture_output=True, text=True,
)
for name in listed.stdout.splitlines():
    path = pathlib.Path(name)
    if not path.is_file() or path.is_symlink():
        continue
    data = path.read_bytes()
    if data.startswith(b"\x7fELF") or b"\0" in data or old_b not in data:
        continue
    path.write_bytes(data.replace(old_b, new_b))
cfg = pathlib.Path(dest, ".config")
if cfg.is_file():
    text = cfg.read_text().replace(
        'BR2_DEFCONFIG="../out-br/defconfig"',
        'BR2_DEFCONFIG="%s/defconfig"' % dest,
    )
    cfg.write_text(text)
PY
	}
	if [[ -L "$SRC/out-br" ]]; then
		rm -f "$SRC/out-br"
	elif [[ -d "$SRC/out-br" ]]; then
		if [[ -e "$dest" ]]; then
			echo "dep-src/out-br and dep-src/out/aarch64/out-br both exist."
			exit 1
		fi
		mv "$SRC/out-br" "$dest"
		rewrite_tree "$SRC/out-br" "$dest"
	fi
	if [[ -d "$old_named" ]]; then
		if [[ -e "$dest" ]]; then
			echo "dep-src/out/aarch64/buildroot and dep-src/out/aarch64/out-br both exist."
			exit 1
		fi
		mv "$old_named" "$dest"
		rewrite_tree "$old_named" "$dest"
	fi
}

# glibc 2.43 defines memchr, bsearch, and wmemchr as _Generic macros.
# m4 1.4.20's gnulib redeclares those names, and the macro then expands
# in the declaration. Undef the macros first. Re-apply in an existing tree.
fix_gnulib_generic() {
	local build="$SRC/out/aarch64/out-br/build"
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

# OpenSSL 3.5 rewrites its Makefile from a parallel `depend` step and
# then fails to move .d.tmp. Its commands call $(MAKE) directly, so
# force those four invocations to one job.
pin_openssl_jobs() {
	local mk="$SRC/buildroot/package/libopenssl/libopenssl.mk"
	[[ -f "$mk" ]] || return 0
	sed -i 's/\$(MAKE) -C \$(@D)/$(MAKE1) -C $(@D)/g' "$mk"
}

# vexpress turns embedded tests on, which logs a watchdog line every second.
pin_optee_demo() {
	local mk="$SRC/build/qemu_v8.mk"
	local conf="$SRC/build/kconfigs/u-boot_qemu_v8.conf"
	if [[ -f "$mk" ]] && ! grep -q CFG_ENABLE_EMBEDDED_TESTS "$mk"; then
		sed -i '/^OPTEE_OS_COMMON_FLAGS += DEBUG=/a OPTEE_OS_COMMON_FLAGS += CFG_ENABLE_EMBEDDED_TESTS=n CFG_TEE_CORE_LOG_LEVEL=2' "$mk"
	fi
	if [[ -f "$conf" ]] && ! grep -q 'fortress.entry' "$conf"; then
		python3 - "$conf" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
old = "load hostfs - ${ramdisk_addr_r} rootfs.cpio.uboot &&  setenv bootargs console=ttyAMA0,115200 earlyprintk=serial,ttyAMA0,115200 root=/dev/ram && bootm"
new = "load hostfs - ${ramdisk_addr_r} rootfs.cpio.uboot && load hostfs - 0x40100000 fortress.entry && env import -t 0x40100000 \\${filesize} && setenv bootargs console=ttyAMA0,115200 earlyprintk=serial,ttyAMA0,115200 root=/dev/ram fortress.entry=\\${fortress_entry} && bootm"
text = path.read_text()
if old not in text:
    raise SystemExit("u-boot bootcmd fragment no longer matches")
path.write_text(text.replace(old, new, 1))
PY
	fi
}

pin_br_out
place_out_br
fix_gnulib_generic
pin_openssl_jobs
pin_optee_demo

MAKE_FLAGS=(
	-C "$SRC/build"
	-f qemu_v8.mk
	OUT_PATH="$SRC/out/aarch64"
	BINARIES_PATH="$SRC/out/aarch64"
	BR_OUT="$SRC/out/aarch64/out-br"
	MEASURED_BOOT_FTPM=n
	RUST_ENABLE=n
	TF_A_TRUSTED_BOARD_BOOT=n
	CFG_TEE_CORE_LOG_LEVEL=2
)

# The IDE exports one CARGO_TARGET_DIR. Two host crates then both try
# to create cargo-target/release/maturin and the second one fails.
unset CARGO_TARGET_DIR

# Host Python cryptography looks for OpenSSL's legacy provider next to
# the system libcrypto. The provider built for this image lives here.
openssl_modules="$SRC/out/aarch64/out-br/per-package/host-libopenssl/host/lib/ossl-modules"
if [[ -d "$openssl_modules" ]]; then
	export OPENSSL_MODULES="$openssl_modules"
fi

echo "Download the ARM GNU toolchain used by OP-TEE 4.10.0 into dep-src/toolchains"
make "${MAKE_FLAGS[@]}" aarch64-toolchain aarch32-toolchain

echo "Build Linux, Buildroot, U-Boot, OP-TEE, and TF-A"
make "${MAKE_FLAGS[@]}" -j"$(nproc)" linux buildroot arm-tf uImage uRootfs

mkdir -p "$SRC/out/aarch64"
ln -sfn "$SRC/out/aarch64/out-br/images/rootfs.cpio.gz" "$SRC/out/aarch64/rootfs.cpio.gz"
"$ROOT/scripts/aarch64/install_guest.sh"

echo "Image is in dep-src/out/aarch64 (bl1.bin, Image, rootfs.cpio.gz)."
echo "TF-A TRUSTED_BOARD_BOOT is off, so this image does not check the boot public key."
echo "Linux console, one at a time: scripts/aarch64/run_SecOC.sh or scripts/aarch64/run_OTA.sh"
echo "OP-TEE console, in another terminal: scripts/aarch64/run_tee.sh"
