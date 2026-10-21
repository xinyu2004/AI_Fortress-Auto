[English](../en/secure-boot.md) | 中文

# 安全启动

启动链不经过 CA。启动公钥只烧一次。公钥写入 OTP eFuse。ROM code 读的是这一段，CLOSED 之后这一段不能再写。这把公钥的哈希烧进 HSM。

随后用同一把公钥核对即将运行的镜像：先是 BL2，然后 BL31，然后 OP-TEE 和 Linux。对上就运行这一级。对不上就停。

客户公钥只在生命周期为开时写入。`boot/provision_boot_key.sh` 计算未压缩公钥点的 SHA-256，并写入 `boot/otp.state`。这个文件是 QEMU 上对这个哈希的替身。`boot/lock_boot_key.sh` 把状态写成 `closed=1`。之后 `otp-provision` 返回锁定错误。这与以后把哈希交给 TF-A 的 ROTPK 是同一条规则。本仓库还没有生成带该哈希的 TF-A 镜像。
