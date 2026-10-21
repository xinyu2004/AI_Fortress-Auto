[English](../en/ca-ta-api.md) | 中文

# CA 与 TA 命令

CA 使用 `TEEC_OpenSession` 与 `TEEC_InvokeCommand`。会话登录是 `TEEC_LOGIN_GROUP`。哪个 Linux 组可以打开哪个 TA，见 [session-login.md](session-login.md)。小整数用 `TEEC_VALUE`，数据块用共享内存。TA 先把共享内存拷进安全世界，再计算。

头文件：[protocol/include/fortress/ta_ids.h](../../protocol/include/fortress/ta_ids.h)。

## ta-ota

| 命令 | 值 | 谁调用 | 做什么 |
| --- | --- | --- | --- |
| `CMD_GET_STATUS` | `0x1001` | ca-ota，UDS `0x22` | 返回 KVN 和 min SVN |
| `CMD_GET_SEED` | `0x1002` | ca-ota，UDS `0x27` | OP-TEE 随机数，种子留在 TA 会话 |
| `CMD_UNLOCK` | `0x1003` | ca-ota，UDS `0x27` | 当前二级公钥验种子签名 |
| `CMD_UPDATE_KEY_MANIFEST` | `0x1004` | ca-ota，UDS `0x31 F000` | 根公钥验 Manifest，KVN 更大才写入 |
| `CMD_IMAGE_BEGIN` | `0x1010` | ca-ota | 验镜像头签名，核对 KVN 与 SVN 下界 |
| `CMD_IMAGE_UPDATE` | `0x1011` | ca-ota，UDS `0x36` | 分块哈希，序号连续才接受 |
| `CMD_IMAGE_VERIFY` | `0x1012` | ca-ota，UDS `0x37` 之后 | 整包哈希与二级公钥验签，还不写 SVN |
| `CMD_IMAGE_ACTIVATE` | `0x1013` | ca-ota，UDS `0x31 F002` | 写入 min SVN。这张镜像用 REE 文件系统。板上写入 RPMB |

## ta-soa

| 命令 | 值 | 谁调用 | 做什么 |
| --- | --- | --- | --- |
| `CMD_SOA_SEAL` | `0x2001` | 感知或规控 | 先查新鲜度，再在 ta-soa 里做 AES-GCM。加密成功后发送计数才加一 |
| `CMD_SOA_OPEN` | `0x2002` | 对端 | 先查接收计数，再做 AES-GCM 打开。旧帧拒绝 |
| `CMD_SOA_INSTALL_KEY` | `0x2003` | ca-ota，开机时 | 把 AES 密钥装进 ta-soa |

失败时计数器不变。返回值使用 GP 的 `TEEC_Result`：签名或 GCM 失败为 `TEE_ERROR_MAC_INVALID` 或 `TEE_ERROR_SIGNATURE_INVALID`，版本和序号失败为 `TEE_ERROR_SECURITY`。

主机上的 `protocol/` 用同一套判决，整数错误码定义在 `fortress/status.h`。AArch64 的 QEMU 镜像里有 ta-ota 和 ta-soa。判决本身由 `make -C protocol test` 覆盖。
