[English](secure-boot.md) | [Chinese](../zh/secure-boot.md)

# Secure boot

The boot chain does not go through a client application. One boot public key is burned once. OTP eFuse holds the hash of that public key. The public key itself is in the boot certificate. ROM code compares the hash, and `burned=1` refuses another write.

On a board, the same public key then checks the image that is about to run: BL2, then BL31, then OP-TEE and Linux. A match runs that image. A mismatch stops.

A customer public key is burned once. `boot/provision_boot_key.sh --pubkey rot_key.pub.pem` writes SHA-256 of the PKCS#1 DER public key into `boot/otp.efuse` and sets `burned=1`. A second run is refused. Set `burned` back to `0` by hand to simulate a new chip. The plain QEMU image does not read this file. `build_qemu.sh secure` copies that hash into BL1 and checks BL2, BL31, OP-TEE, and U-Boot. That U-Boot checks the kernel and ramdisk in `fit.itb`. The plain image boots the same FIT and does not require the signature. The steps are in [trusted-board-boot.md](trusted-board-boot.md).
