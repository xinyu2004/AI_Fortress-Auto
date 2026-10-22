[English](porting.md) | [Chinese](../zh/porting.md)

# Porting to your board

Do not copy the CAs, TAs, or `protocol/` per chip. Copy `scripts/aarch64/platform.env` and change `CROSS_COMPILE` and `OPTEE_PLATFORM`. The 32-bit file is `scripts/aarch32/platform.env`. `prepare_env.sh` copies that file to `dep-src/env-aarch64.sh` or `dep-src/env-aarch32.sh`. `build_qemu.sh` and `run_linux.sh` keep their own machine lines. `versions.env` stays the shared list of upstream tags.

1. Confirm the chip is a Cortex-A with TrustZone. Cortex-M and TriCore do not run this OP-TEE tree. They can still reuse the messages and the PKCS#11 side.
2. Fill in the UART, clock, GIC, core count, secure-DRAM base and size, and the `reserved-memory` and `optee` nodes in the device tree. This QEMU image does not read those values.
3. Add `scripts/build_<board>.sh`. Change only `CROSS_COMPILE` and the OP-TEE `PLATFORM`. The compile steps match `scripts/aarch64/build.sh`. Do not make that script download sources on its own.
4. KVN and min SVN stay in the TA persistent object. This tree does not use RPMB.
5. A board token replaces the OP-TEE PKCS#11 TA. The TA calls stay the same. The steps are in [switch-pkcs11.md](switch-pkcs11.md).
6. In the factory state, `boot/provision_boot_key.sh --pubkey rot_key.pub.pem` burns the boot public-key hash. A second run is refused until `burned` is set back to `0`. The QEMU steps are in [trusted-board-boot.md](trusted-board-boot.md).
7. Acceptance is still boot verification, chunked download, KVN/SVN, and perception/planning messages. Command numbers stay the same.

The tags in `versions.env` are the clone targets. A new board does not change them unless a board patch needs another OP-TEE version.

`scripts/prepare_sources.sh` fills `dep-src/` for both architectures. It skips the download when the checkout already matches the pinned commit. Each architecture writes its own tree under `dep-src/out/`.
