[English](../en/build-qemu.md) | 中文

# QEMU 镜像，第一阶段

这一阶段让 AArch64 的 QEMU 启动，启动链里有 OP-TEE。rootfs 里有 ta-ota 和 ta-soa。SecOC 启动 perception 和 planning。OTA 等诊断仪。TF-A 的 Trusted Board Boot 保持关闭，所以这张镜像还不检查启动公钥。这台 virt 机器没有 eMMC 或 UFS。mmc-utils 仍为以后的 eMMC 板子留着，这里没有设备可开。这张镜像上的 KVN 和 min SVN 在 REE 文件系统。每次启动回到 KVN 1 和 min SVN 1。

版本对齐 OP-TEE manifest 标签 4.10.0 里的 `qemu_v8.xml` 和 `common.xml`。tag 已用 `git ls-remote` 对过。

| 树 | tag |
| --- | --- |
| optee_os、optee_client、optee_test、build | 4.10.0 |
| optee_examples | 4.10.0 |
| trusted-firmware-a | v2.14.0 |
| linux（linaro-swg/linux） | optee-4.10.0 |
| buildroot | 2025.05 |
| u-boot | v2025.07 |

原先 `versions.env` 钉的是 TF-A v2.12.0、kernel.org 的 Linux v6.12.49、Buildroot 2025.02.1。这三样不是这份配方在编的组合。

```bash
scripts/prepare_sources.sh
scripts/aarch64/prepare_env.sh
scripts/aarch64/build_qemu.sh
```

打开镜像的步骤在 README。SecOC 是 `run_SecOC.sh` 加 `run_tee.sh`。OTA 是 `run_OTA.sh` 加 `run_tee.sh`，然后 `scripts/diag_tester.sh`。

`prepare_sources.sh` 把源码放到 `dep-src/`。`scripts/aarch64/prepare_env.sh` 检查主机上的 `bison`、`flex` 和 `dtc`，缺了就给出安装命令。`scripts/aarch64/build_qemu.sh` 不再下载这些树，也不装包。它把 OP-TEE 4.10.0 使用的 ARM GNU 工具链下到 `dep-src/toolchains`，再通过 `dep-src/build/qemu_v8.mk` 编译 Linux、Buildroot、U-Boot、OP-TEE 和 TF-A。fTPM 和 Rust 示例关掉。脚本关掉 U-Boot 的主机工具 `mkeficapsule`，所以编译不需要 `gnutls.h`。脚本不编译 QEMU 本体。Buildroot 的工作树在 `dep-src/out/aarch64/out-br`（`extra.conf`、`build`、`host`、`staging`、`target`、`images`）。启动文件仍在 `dep-src/out/aarch64`。AArch32 镜像由 `scripts/aarch32/build_qemu.sh` 编到 `dep-src/out/aarch32`。镜像进入 `dep-src/out/aarch64` 之后，`scripts/aarch64/run_SecOC.sh` 或 `scripts/aarch64/run_OTA.sh` 是 Linux 窗口，一次只跑一个。另一个 SSH 会话跑 `scripts/aarch64/run_tee.sh`，那是 OP-TEE。`bl1.bin`、`Image`、`rootfs.cpio.gz` 还没有时，Linux 脚本打印缺什么和编译命令，然后退出。`scripts/aarch64/install_guest.sh` 把 ta-ota、ta-soa、perception、planning 和 ca-ota 打进这张 rootfs。`run_SecOC.sh` 写入 `fortress.entry=secoc`。Linux 起来后，perception 和 planning 每 100 ms 交换一帧新明文。`run_OTA.sh` 写入 `fortress.entry=ota`，并加上 virtio 网卡，把 TCP 27100 转到主机。ca-ota 等待。`scripts/diag_tester.sh` 先做 `0x27` 解锁，再送入 UDS 成功路径，验签 Key Manifest 和分包。KVN 和 min SVN 经 `tee-supplicant` 存在 REE 文件系统里（`CFG_REE_FS=y`）。这不是 RPMB。Linux 窗口交给 BL1 的 flash 镜像把 `bl1.bin` 放在偏移 0、`fip.bin` 放在 `0x40000`，BL1 从这里装 BL2。U-Boot 仍通过 semihosting 装内核和 rootfs。OTA 在诊断仪触发前保持等待。Linux 窗口到 Buildroot 的登录提示。

`scripts/aarch64/build.sh` 仍然只编 OP-TEE OS 和 optee_client 到 `dep-src/out/aarch64`，并带上 `CFG_USER_TA_TARGETS=ta_arm64`，给主机侧的检查用，不编这张镜像。

`scripts/aarch32/build_qemu.sh` 把 AArch32 镜像编到 `dep-src/out/aarch32/out-br` 和 `dep-src/out/aarch32`。Linux 和 U-Boot 用单独的工作树，OP-TEE 和 TF-A 用各自的输出目录，所以这次编译不会换掉 AArch64 镜像。SecOC 是 `scripts/aarch32/run_SecOC.sh` 和 `scripts/aarch32/run_tee.sh`。OTA 是 `scripts/aarch32/run_OTA.sh` 和 `scripts/aarch32/run_tee.sh`，然后 `scripts/diag_tester.sh aarch32`。
