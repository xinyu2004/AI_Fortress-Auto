[English](build-qemu.md) | [Chinese](../zh/build-qemu.md)

# QEMU image, first stage

This stage boots an AArch64 QEMU machine with OP-TEE in the boot chain. ta-ota and ta-soa are in the rootfs. SecOC starts perception and planning. OTA waits for the diagnostic tester. Trusted Board Boot stays off, so this image does not check the boot public key. This virt machine has no eMMC or UFS device. mmc-utils stays selected for a later eMMC board and has nothing to open here. KVN and min SVN on this image use the REE filesystem. Each boot starts again at KVN 1 and min SVN 1.

The pins match the OP-TEE manifest tag 4.10.0, files `qemu_v8.xml` and `common.xml`. Tags were checked with `git ls-remote`.

| Tree | Tag |
| --- | --- |
| optee_os, optee_client, optee_test, build | 4.10.0 |
| optee_examples | 4.10.0 |
| trusted-firmware-a | v2.14.0 |
| linux (linaro-swg/linux) | optee-4.10.0 |
| buildroot | 2025.05 |
| u-boot | v2025.07 |
| mbedtls | v3.6.5 |

`versions.env` used to pin TF-A v2.12.0, kernel.org Linux v6.12.49, and Buildroot 2025.02.1. Those three are not the combination this recipe builds. mbed TLS is used only by `build_qemu.sh secure`. The plain image does not link it.

```bash
scripts/prepare_sources.sh
scripts/aarch64/prepare_env.sh
scripts/aarch64/build_qemu.sh
```

Open the image from the README: SecOC is `run_SecOC.sh` plus `run_tee.sh`. OTA is `run_OTA.sh` plus `run_tee.sh`, then `scripts/diag_tester.sh`. The plain `build_qemu.sh` leaves the boot check off and writes `fit.itb`. After that image exists, `build_qemu.sh secure` checks BL2, BL31, OP-TEE, and U-Boot, and that U-Boot checks the kernel and ramdisk in the same `fit.itb`. Boot it with `run_SecOC.sh secure` or `run_OTA.sh secure`. The steps are in [trusted-board-boot.md](trusted-board-boot.md).

`prepare_sources.sh` clones into `dep-src/`. `scripts/aarch64/prepare_env.sh` checks for `bison`, `flex`, and `dtc` on the host and prints the install command when one is missing. `scripts/aarch64/build_qemu.sh` does not download those trees and does not install packages. It downloads the ARM GNU toolchain that OP-TEE 4.10.0 uses, into `dep-src/toolchains`, then builds Linux, Buildroot, U-Boot, OP-TEE, and TF-A through `dep-src/build/qemu_v8.mk`. fTPM and the Rust examples are turned off. The script turns off U-Boot's host tool `mkeficapsule`, so the build does not need `gnutls.h`. It does not compile QEMU itself. Buildroot's work tree is `dep-src/out/aarch64/out-br` (`extra.conf`, `build`, `host`, `staging`, `target`, `images`). The boot files stay in `dep-src/out/aarch64`. The AArch32 image is built by `scripts/aarch32/build_qemu.sh` into `dep-src/out/aarch32`. After the image is in `dep-src/out/aarch64`, `scripts/aarch64/run_SecOC.sh` or `scripts/aarch64/run_OTA.sh` is the Linux window. Run one of them. The other SSH session runs `scripts/aarch64/run_tee.sh` for OP-TEE. Until `bl1.bin`, `Image`, and `rootfs.cpio.gz` exist, the Linux scripts print the missing names and the build command, then exit. `scripts/aarch64/install_guest.sh` packs ta-ota, ta-soa, perception, planning, and ca-ota into that rootfs. `run_SecOC.sh` writes `fortress.entry=secoc`. After Linux boots, perception and planning exchange a new plaintext every 100 ms. `run_OTA.sh` writes `fortress.entry=ota` and adds a virtio NIC with TCP port 27100 forwarded to the host. ca-ota waits. `scripts/diag_tester.sh` unlocks with `0x27`, then sends the successful UDS path for a signed key manifest and signed chunks. KVN and min SVN are stored through `tee-supplicant` (`CFG_REE_FS=y`). That store is not RPMB. The Linux window passes BL1 a flash image with `bl1.bin` at offset 0 and `fip.bin` at `0x40000`, so BL1 loads BL2 from that package. U-Boot still loads the kernel and rootfs through semihosting. The Linux window reaches the Buildroot login. OTA stays idle until the host tester runs.

`scripts/aarch64/build.sh` still builds OP-TEE OS and optee_client into `dep-src/out/aarch64`, with `CFG_USER_TA_TARGETS=ta_arm64`. It does not build this image.

`scripts/aarch32/build_qemu.sh` builds the AArch32 image into `dep-src/out/aarch32/out-br` and `dep-src/out/aarch32`. Linux and U-Boot are built from separate worktrees, and OP-TEE and TF-A use their own output directories, so this build does not replace the AArch64 image. SecOC is `scripts/aarch32/run_SecOC.sh` and `scripts/aarch32/run_tee.sh`. OTA is `scripts/aarch32/run_OTA.sh` and `scripts/aarch32/run_tee.sh`, then `scripts/diag_tester.sh aarch32`.
