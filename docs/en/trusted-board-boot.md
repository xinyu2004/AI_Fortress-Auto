# Trusted Board Boot on this QEMU

The plain image and the secure image are two builds. Both switches are fixed at compile time. Nothing at boot turns checking on or off.

`scripts/aarch64/build_qemu.sh` and `scripts/aarch32/build_qemu.sh` pass `TF_A_TRUSTED_BOARD_BOOT=n`. BL1 does not check a public key. `GENERATE_COT` stays off, so the FIP has no certificate chain. `build_qemu.sh` does not read `boot/otp.efuse`.

`build_qemu.sh secure` passes `TF_A_TRUSTED_BOARD_BOOT=y` and `GENERATE_COT=1`. `GENERATE_COT` signs the certificates while compiling: `tb_fw.crt` for BL2, `soc_fw_*.crt` for BL31, `tos_fw_*.crt` for OP-TEE, and `nt_fw_*.crt` for U-Boot. `TRUSTED_BOARD_BOOT` compiles the check into BL1 and BL2. BL1 checks BL2. BL2 checks BL31, OP-TEE, and U-Boot. A mismatched block stops in TF-A and does not reach U-Boot.

The boot public key is an RSA-2048 key. It is not the P-256 key used for OTA. The private key stays in `dep-src/boot-keys/rot_key.pem`. The hash in `boot/otp.efuse` is SHA-256 of that public key in PKCS#1 DER, which is the same encoding TF-A stores in BL1. QEMU does not read `boot/otp.efuse` at boot. The secure build copies the hash into BL1.

`burned=0` is a new chip. The hash can still be written. `burned=1` means the hash is already burned, and `boot/provision_boot_key.sh` refuses another write. This file is a stand-in for eFuse. A real eFuse cannot be cleared. To simulate a new chip, set `burned` back to `0` by hand.

The plain image has to exist first. Both images boot `dep-src/out/<arch>/fit.itb`, which holds the kernel, the QEMU device tree, and the ramdisk. The plain U-Boot does not require a signature on that file. The secure build signs it with the boot private key and puts the public key into the U-Boot inside `dep-src/out/<arch>/secure/`. That U-Boot stops if the signature is missing or wrong. Linux, the ramdisk, and OP-TEE are the same bytes in both images.

```bash
mkdir -p dep-src/boot-keys
openssl genrsa -out dep-src/boot-keys/rot_key.pem 2048
openssl rsa -in dep-src/boot-keys/rot_key.pem -pubout -out dep-src/boot-keys/rot_key.pub.pem
boot/provision_boot_key.sh --pubkey dep-src/boot-keys/rot_key.pub.pem
scripts/aarch64/build_qemu.sh secure
```

`scripts/prepare_sources.sh` clones mbed TLS, which the secure TF-A build needs. The 32-bit command is `scripts/aarch32/build_qemu.sh secure`.

Boot the plain image with `scripts/aarch64/run_SecOC.sh`. Boot the secure image with `scripts/aarch64/run_SecOC.sh secure`. OTA is the same, with `run_OTA.sh` and `run_OTA.sh secure`. One QEMU at a time. The 32-bit scripts take the same `secure` argument.
