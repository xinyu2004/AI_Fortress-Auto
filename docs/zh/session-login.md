[English](../en/session-login.md) | 中文

# 谁可以打开 TA

CA 通过 `TEEC_OpenSession` 进入 TA。本手册的登录方式是 `TEEC_LOGIN_GROUP`。内核用 Linux 组号生成 `gpd.client.identity`。optee_client 里的 `libteeacl` 在普通世界算出同一个身份，并检查进程是否属于这个组。`scripts/aarch64/build.sh` 和 `scripts/aarch32/build.sh` 传入 `WITH_TEEACL=1`，从而编出 `libteeacl.so`，落在 `dep-src/out/aarch64/optee_client` 和 `dep-src/out/aarch32/optee_client`。客户机上的 CA 链接 `-lteeacl`。这个库链接 `libuuid`。构建脚本把 util-linux `v2.41.3` 的 `libuuid` 编到 `dep-src/sysroot/`，给交叉编译器使用。

## 组

| 组 | 进程 | 可以打开的 TA | 命令 |
| --- | --- | --- | --- |
| `fortress-ota` | ca-ota | ta-ota | 密钥清单、分块、激活、状态、解锁 |
| `fortress-ota` | ca-ota | ta-soa | 仅 `CMD_SOA_INSTALL_KEY`（`0x2003`） |
| `fortress-perception` | perception | ta-soa | `CMD_SOA_SEAL`（`0x2001`）、`CMD_SOA_OPEN`（`0x2002`） |
| `fortress-planning` | planning | ta-soa | `CMD_SOA_SEAL`（`0x2001`）、`CMD_SOA_OPEN`（`0x2002`） |

激活和一帧 SecOC 都会改变车辆的行为。允许发出这些请求的进程，就是负责该功能的进程。不在组里的进程打不开会话，命令进不了 TA。

TA 读取 `gpd.client.identity`，只接受表中该组的命令。会话打开之后，TA 仍然核对签名、新鲜度、KVN 和 min SVN。

## 调用

CA 链接 `-lteeacl`，并包含 `<teeacl.h>`。

1. `teeacl_gid_from_name()` 把组名变成 `gid`。
2. `teeacl_current_user_is_member_of()` 必须返回 `IS_MEMBER`。
3. `teeacl_group_acl_uuid()` 写出 TA 会看到的 `group:` 身份。它用的命名空间与内核相同，`58ac9ca0-2086-4683-a1b8-ec4bc08e01b6`。
4. `TEEC_OpenSession` 使用 `TEEC_LOGIN_GROUP`，并传入这个 `gid`。

客户机上 `scripts/aarch64/run_SecOC.sh` 或 `scripts/aarch32/run_SecOC.sh` 启动 perception 和 planning。ca-ota 先把 AES 密钥装进 ta-soa，然后这两个进程再打开会话。这次登录走表里的组。
