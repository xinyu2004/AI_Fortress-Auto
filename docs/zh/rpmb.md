[English](../en/rpmb.md) | 中文

# RPMB

![eMMC](../img/zh/emmc.svg)

编程会话的判决是一次版本检查。RPMB 里放用来比较的两个数：KVN 和 min SVN。`0x31 F000` 把它们放在 Key Manifest 里。`0x31 F002` 不下发这两个数，镜像头里已经有。上图就是这次比较。这块区域常见容量 128 KiB 到 4 MiB，访问单位 256 字节。颗粒厂发货时，256 位 HMAC 密钥槽是空的。客户下线写入一次，密钥由 HUK 推出，这样已保存的数退不回去。设备不把密钥读出来。

之后每次读写都是一帧：地址、数据、设备内部的写计数、HMAC-SHA256。成功写入后设备把计数加一。帧被拒绝、设备接受之前掉电，或重放旧帧，计数和已保存的值都不变。

![UFS](../img/zh/ufs.svg)

UFS 上，同一套规则用在 RPMB well-known logical unit，JESD220 编号是 `0xC4`。UFS 2.2 和 UFS 3.1 最多四个 RPMB region，各自有自己的密钥和写计数。KVN 和 min SVN 用其中一个。`mmc` 打不开这个单元。

OP-TEE 在安全世界组帧并计算 MAC。帧以 `OPTEE_RPC_CMD_RPMB` 交给 `tee-supplicant`。它是普通世界里的守护进程，不是 CA，把同一帧交给驱动，再把应答送回。它算不出合法 MAC。只做一次的密钥写入走同样的两跳。这一步没有 ca-ota。之后 ca-ota 调用 ta-ota 更新二级公钥或执行激活，调用停在 ta-ota。普通世界如果抢先写入这把密钥，TEE 以后就用不了这块区域。

KVN 只有在根公钥验过一把更大的新二级公钥时才改变。min SVN 只有在整包验签通过、头里的 KVN 等于已保存的 KVN、头里的 SVN 不低于已保存的下限、并且执行激活时，才写成这个包的 SVN。SecOC 的新鲜度留在 TA 里，只在这次上电有效。

eMMC 板上，Buildroot 可以装上 mmc-utils 的 `mmc`（`BR2_PACKAGE_MMC_UTILS=y`）。它从 Linux 发 MMC ioctl，包括读写计数和这一次密钥写入。tee-supplicant 不链接它。如果 `mmc` 比 TEE 先写入密钥，TEE 就用不了这块区域。

## 设备上没有 RPMB

`/data/tee` 只有在 OP-TEE 编译时打开 `CFG_REE_FS=y` 才有。这个选项选用 REE 文件系统后端，`tee-supplicant` 才把对象存进这个目录。`CFG_REE_FS=n` 时没有 `/data/tee`。文件用 HUK 推出的密钥加密，文件被改能发现。把整个目录换回旧副本，校验仍然通过，挡不住回滚。KVN 和 min SVN 要改接到另一个只增不减的锚：SoC 的单调计数器，或 HSM 里的 NV 计数器。min SVN 也可以烧在 OTP 熔丝位上。这些位很少，只用来记下限。没有任何可用的锚时，不把这两个数当成已经写下。

这张 QEMU virt 机器没有 eMMC，也没有 UFS，所以 `mmc` 没有设备可开。镜像仍为以后的 eMMC 板子选上 mmc-utils。这张镜像上的 KVN 和 min SVN 在 REE 文件系统。每次启动回到 KVN 1 和 min SVN 1。客户机不检查设备里的写计数。
