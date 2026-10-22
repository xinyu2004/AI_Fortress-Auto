[English](../en/switch-pkcs11.md) | 中文

# 接上 PKCS#11 令牌

业务命令不变。CA 仍然打开 ta-ota 或 ta-soa，不打开令牌。

这张 QEMU 镜像上的令牌是 OP-TEE 的 PKCS#11 TA。ta-ota 和 ta-soa 从安全世界调用它。它们不调用 `TEE_ALG_AES_GCM` 或 `TEE_ALG_ECDSA_P256`。`config/hsm.env` 没有被读取。分工见 [hsm.md](hsm.md)。

客户端代码放在这两个 TA 旁边。它打开一个会话，第一次使用时初始化令牌并登录，然后按标签创建或替换对象。AES 密钥的 16 字节在对象建好后从 TA 的栈上清掉。ta-soa 不再把这把密钥写入 `fortress.soa` 持久对象。ta-ota 继续在 `fortress.ota` 里保存 KVN、min SVN 和根公钥哈希。

客户机根文件系统里要有 `fd02c9da-306c-48c7-a49c-bbd827ae86ee.ta`。

`libckteec.so` 仍是普通世界的工具。`scripts/check_pkcs11.sh` 和 `pkcs11-tool` 用它查看令牌。普通世界的 `pkcs11-tool` 会话会被拒绝。SecOC 和 OTA 跑通才说明令牌在工作。

```bash
pkcs11-tool --module <so> -I
pkcs11-tool --module <so> -L
pkcs11-tool --module <so> -M
```

`-M` 的结果里要有 ECDSA 和 AES-GCM。这份列表用来查看板上的令牌，不是 ta-soa 的调用路径。

以后的板子如果在安全世界里有 PKCS#11 令牌，业务 TA 的调用不变，换掉的是令牌。只有 SHE 密钥槽、没有安全世界 PKCS#11 令牌的芯片，HAL 里做两件事：P-256 验签和 AES-128-GCM。KVN、min SVN、分块序号和新鲜度仍由 TA 决定。
