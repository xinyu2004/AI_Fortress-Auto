# Shared FIT for the plain and secure images.
# The FIT holds the kernel, the QEMU device tree, and the ramdisk.
# build_qemu.sh writes it unsigned. build_qemu.sh secure signs that same file
# and embeds the public key into the U-Boot that goes into the secure FIP.
# The plain U-Boot boots this file and does not require the signature.

fit_arch_paths() {
	local arch="$1"
	FIT_KERNEL= FIT_ARCH= FIT_LOAD= FIT_ENTRY= FIT_ADDR= FIT_ENTRY_ADDR=
	FIT_UBOOT_SRC= FIT_CROSS= FIT_DEFCONFIG= FIT_QEMU=()
	if [[ "$arch" == "aarch64" ]]; then
		FIT_KERNEL=Image
		FIT_ARCH=arm64
		FIT_LOAD=0x42200000
		FIT_ENTRY=0x42200000
		FIT_ADDR=0x50000000
		FIT_ENTRY_ADDR=0x40100000
		FIT_RAMDISK_LOAD=-
		FIT_UBOOT_SRC="$SRC/u-boot"
		FIT_CROSS="$SRC/toolchains/aarch64/bin/aarch64-linux-gnu-"
		FIT_DEFCONFIG=qemu_arm64_defconfig
		FIT_QEMU=(
			qemu-system-aarch64
			-cpu max,sme=on,pauth-impdef=on
			-machine virt,acpi=off,secure=on,mte=off,gic-version=3,virtualization=off,dumpdtb="$2/virt.dtb"
			-smp 2 -m 1057 -display none
			-netdev user,id=net0 -device virtio-net-pci,netdev=net0
		)
		# QEMU's dump has no OP-TEE node. Linux only probes the driver from
		# this node, and the two ranges are the ones this image already boots.
		FIT_OPTEE_SHM=0x42000000
	else
		FIT_KERNEL=zImage
		FIT_ARCH=arm
		FIT_LOAD=0x40400000
		FIT_ENTRY=0x40400000
		FIT_ADDR=0x48000000
		FIT_ENTRY_ADDR=0x40200000
		FIT_RAMDISK_LOAD=0x60000000
		FIT_UBOOT_SRC="$SRC/u-boot-arm"
		FIT_CROSS="$SRC/toolchains/aarch32/bin/arm-linux-gnueabihf-"
		FIT_DEFCONFIG=qemu_arm_defconfig
		FIT_QEMU=(
			qemu-system-arm
			-cpu cortex-a15
			-machine virt,secure=on,dumpdtb="$2/virt.dtb"
			-smp 2 -m 1057 -display none
			-netdev user,id=net0 -device virtio-net-device,netdev=net0
		)
		FIT_OPTEE_SHM=0x7fe00000
	fi
}

fit_dump_dtb() {
	local arch="$1" out="$2"
	fit_arch_paths "$arch" "$out"
	if [[ ! -s "$out/virt.dtb" ]]; then
		"${FIT_QEMU[@]}"
	fi
	[[ -s "$out/virt.dtb" ]]
	fit_annotate_dtb "$out"
}

# The dumped tree is what QEMU creates before firmware boot. Linux needs the
# OP-TEE node that the previous image received from QEMU's kernel loader.
fit_annotate_dtb() {
	local out="$1"
	local dts="$out/virt.dts"
	dtc -I dtb -O dts -o "$dts" "$out/virt.dtb" 2>/dev/null
	if grep -q 'interrupts = <0x00 0xbb 0x01>;' "$dts"; then
		return 0
	fi
	python3 - "$dts" "$FIT_OPTEE_SHM" <<'PY'
import pathlib, sys
path, shm = sys.argv[1:]
shm = int(shm, 16)
file = pathlib.Path(path)
text = file.read_text()
irq = '\t\t\tinterrupts = <0x00 0xbb 0x01>;\n'
method = '\t\t\tmethod = "smc";\n'
if method in text and irq not in text:
    file.write_text(text.replace(method, method + irq, 1))
    raise SystemExit(0)
needle = '\tcompatible = "linux,dummy-virt";\n'
block = needle + f"""
	reserved-memory {{
		#address-cells = <0x02>;
		#size-cells = <0x02>;
		ranges;
		optee_core@e100000 {{
			reg = <0x00 0x0e100000 0x00 0x00f00000>;
			no-map;
		}};
		optee_shm@{shm:x} {{
			reg = <0x00 {shm:#x} 0x00 0x00200000>;
			no-map;
		}};
	}};
	firmware {{
		optee {{
			compatible = "linaro,optee-tz";
			method = "smc";
			interrupts = <0x00 0xbb 0x01>;
		}};
	}};
"""
if needle not in text:
    raise SystemExit("virt.dtb root compatible line no longer matches")
file.write_text(text.replace(needle, block, 1))
PY
	dtc -I dts -O dtb -o "$out/virt.dtb" "$dts" 2>/dev/null
}

fit_write_its() {
	local arch="$1" out="$2" sign="$3"
	fit_arch_paths "$arch" "$out"
	python3 - "$out/fit.its" "$FIT_KERNEL" "$FIT_ARCH" "$FIT_LOAD" "$FIT_ENTRY" "$sign" "$FIT_RAMDISK_LOAD" <<'PY'
import pathlib, sys
path, kernel, arch, load, entry, sign, ramdisk_load = sys.argv[1:]
sig = ""
if sign == "sign":
    sig = """
                        signature {
                                algo = "sha256,rsa2048";
                                key-name-hint = "rot";
                        };"""
rd_load = ""
if ramdisk_load != "-":
    rd_load = f"\n                        load = <{ramdisk_load}>;"
text = f"""/dts-v1/;
/ {{
        description = "Fortress QEMU";
        #address-cells = <1>;
        images {{
                kernel {{
                        description = "Linux";
                        data = /incbin/("{kernel}");
                        type = "kernel";
                        arch = "{arch}";
                        os = "linux";
                        compression = "none";
                        load = <{load}>;
                        entry = <{entry}>;
                        hash-1 {{ algo = "sha256"; }};
                }};
                fdt {{
                        description = "QEMU virt";
                        data = /incbin/("virt.dtb");
                        type = "flat_dt";
                        arch = "{arch}";
                        compression = "none";
                        hash-1 {{ algo = "sha256"; }};
                }};
                ramdisk {{
                        description = "rootfs";
                        data = /incbin/("rootfs.cpio.gz");
                        type = "ramdisk";
                        arch = "{arch}";
                        os = "linux";
                        compression = "gzip";{rd_load}
                        hash-1 {{ algo = "sha256"; }};
                }};
        }};
        configurations {{
                default = "conf";
                conf {{
                        description = "boot";
                        kernel = "kernel";
                        fdt = "fdt";
                        ramdisk = "ramdisk";{sig}
                }};
        }};
}};
"""
pathlib.Path(path).write_text(text)
PY
}

# Unsigned FIT. Plain U-Boot boots it. Secure signing replaces this file.
fit_pack() {
	local arch="$1" out="$2"
	fit_dump_dtb "$arch" "$out"
	fit_write_its "$arch" "$out" no
	for need in "$out/$FIT_KERNEL" "$out/rootfs.cpio.gz" "$out/virt.dtb"; do
		if [[ ! -f "$need" ]]; then
			echo "FIT is missing $need"
			exit 1
		fi
	done
	(
		cd "$out"
		"$FIT_UBOOT_SRC/tools/mkimage" -f fit.its fit.itb
	)
	echo "FIT is $out/fit.itb"
}

# Sign the shared FIT and link a U-Boot that requires that signature.
# The binary is copied to $out/u-boot-secure/u-boot.bin.
fit_sign() {
	local arch="$1" out="$2" priv="$3"
	local tree="$SRC/u-boot-fit-$arch"
	local frag embed control
	fit_pack "$arch" "$out"
	if [[ "$arch" == "aarch64" ]]; then
		frag="$SRC/build/kconfigs/u-boot_qemu_v8.conf"
	else
		frag="$SRC/build/kconfigs/u-boot_qemu_virt_v7.conf"
	fi
	# U-Boot refuses an out-of-tree build while the source tree has a .config.
	# A detached worktree stays separate from the plain image's U-Boot.
	if [[ ! -f "$tree/Makefile" ]]; then
		git -C "$FIT_UBOOT_SRC" worktree add --detach "$tree" HEAD
	fi
	# QEMU's board file takes the control FDT from the start of RAM, so a key
	# stored anywhere else is ignored. Embedding requires OF_BOARD off, which
	# this U-Boot version rejects unless the board is listed.
	if ! grep -qx 'qemu-arm' "$tree/scripts/of_allowlist.txt"; then
		echo 'qemu-arm' >>"$tree/scripts/of_allowlist.txt"
	fi
	make -C "$tree" CROSS_COMPILE="$FIT_CROSS" "$FIT_DEFCONFIG"
	embed="$tree/fortress-embed.conf"
	cat >"$embed" <<'EOF'
CONFIG_OF_EMBED=y
# CONFIG_OF_SEPARATE is not set
# CONFIG_OF_BOARD is not set
# CONFIG_OF_OMIT_DTB is not set
EOF
	"$tree/scripts/kconfig/merge_config.sh" -m -O "$tree" "$tree/.config" "$frag" "$embed"
	make -C "$tree" CROSS_COMPILE="$FIT_CROSS" olddefconfig
	make -C "$tree" CROSS_COMPILE="$FIT_CROSS" EXT_DTB="$out/virt.dtb" -j"$(nproc)"
	control="$out/control.dtb"
	cp "$out/virt.dtb" "$control"
	fit_write_its "$arch" "$out" sign
	ln -sfn "$priv" "$SRC/boot-keys/rot.key"
	# mkimage looks up <hint>.key and <hint>.crt. The certificate is only
	# the wrapper mkimage reads; the public key written into U-Boot is the
	# same RSA key BL1 already checks.
	if [[ ! -f "$SRC/boot-keys/rot.crt" ]]; then
		openssl req -new -x509 -key "$priv" -out "$SRC/boot-keys/rot.crt" \
			-days 36500 -subj "/CN=rot/"
	fi
	(
		cd "$out"
		"$tree/tools/mkimage" -f fit.its fit.itb
		"$tree/tools/mkimage" -F -k "$SRC/boot-keys" -K control.dtb -r fit.itb
	)
	make -C "$tree" CROSS_COMPILE="$FIT_CROSS" EXT_DTB="$control" -j"$(nproc)"
	mkdir -p "$out/u-boot-secure"
	cp "$tree/u-boot.bin" "$out/u-boot-secure/u-boot.bin"
	dtc -I dtb -O dts -o "$out/control.dts" "$control" 2>/dev/null || true
	if ! grep -q 'key-name-hint = "rot"' "$out/control.dts"; then
		echo "The secure U-Boot DTB does not contain the boot public key." >&2
		exit 1
	fi
	if ! grep -a -q 'key-name-hint' "$out/u-boot-secure/u-boot.bin"; then
		echo "The boot public key was not linked into the secure U-Boot." >&2
		exit 1
	fi
}
