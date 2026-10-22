[English](../en/secure-boot.md) | 中文

# 安全启动

启动链不经过 CA。启动公钥只烧一次。OTP eFuse 里是这把公钥的哈希。公钥本身在启动证书里。ROM code 比对这份哈希，`burned=1` 之后不能再写。

在板子上，随后用同一把公钥核对即将运行的镜像：先是 BL2，然后 BL31，然后 OP-TEE 和 Linux。对上就运行这一级。对不上就停。

`boot/provision_boot_key.sh --pubkey rot_key.pub.pem` 把 PKCS#1 DER 公钥的 SHA-256 写入 `boot/otp.efuse`，并写成 `burned=1`。再执行会被拒绝。要模拟一颗新芯片，把 `burned` 手工改回 `0`。`build_qemu.sh secure` 把这份哈希编进 BL1，核对 BL2、BL31、OP-TEE 和 U-Boot。这份 U-Boot 再核对 `fit.itb` 里的内核和 ramdisk。普通镜像启动同一个 FIT，不要求这个签名。步骤在 [trusted-board-boot.md](../en/trusted-board-boot.md)。
