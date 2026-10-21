[English](session-login.md) | [Chinese](../zh/session-login.md)

# Who may open a TA

A client application reaches a TA through `TEEC_OpenSession`. The login method for this handbook is `TEEC_LOGIN_GROUP`. The kernel builds `gpd.client.identity` from the Linux group id. `libteeacl` in optee_client computes that same identity in normal world and checks that the process is a member of the group. `scripts/aarch64/build.sh` and `scripts/aarch32/build.sh` pass `WITH_TEEACL=1`, which builds `libteeacl.so` under `dep-src/out/aarch64/optee_client` and `dep-src/out/aarch32/optee_client`. The guest CA links `-lteeacl`. The library links `libuuid`. The build script compiles util-linux `v2.41.3` `libuuid` into `dep-src/sysroot/` for the cross compiler.

## Groups

| Group | Process | TA it may open | Commands |
| --- | --- | --- | --- |
| `fortress-ota` | ca-ota | ta-ota | Key manifest, chunks, activate, status, unlock |
| `fortress-ota` | ca-ota | ta-soa | `CMD_SOA_INSTALL_KEY` (`0x2003`) only |
| `fortress-perception` | perception | ta-soa | `CMD_SOA_SEAL` (`0x2001`), `CMD_SOA_OPEN` (`0x2002`) |
| `fortress-planning` | planning | ta-soa | `CMD_SOA_SEAL` (`0x2001`), `CMD_SOA_OPEN` (`0x2002`) |

Activation and a SecOC frame both change how the vehicle behaves. The process allowed to request them is the process that owns that function. A process outside the group does not get a session, so the command never enters the TA.

The TA reads `gpd.client.identity` and accepts a command only from the group in the table. After the session is open, the TA still checks the signature, the freshness value, KVN, and min SVN.

## Calls

The CA links `-lteeacl` and includes `<teeacl.h>`.

1. `teeacl_gid_from_name()` resolves the group name to a `gid`.
2. `teeacl_current_user_is_member_of()` must return `IS_MEMBER`.
3. `teeacl_group_acl_uuid()` writes the `group:` identity the TA will see. It uses the same namespace as the kernel, `58ac9ca0-2086-4683-a1b8-ec4bc08e01b6`.
4. `TEEC_OpenSession` uses `TEEC_LOGIN_GROUP` and passes that `gid`.

On the guest, `scripts/aarch64/run_SecOC.sh` or `scripts/aarch32/run_SecOC.sh` starts perception and planning. ca-ota installs the AES key before they open ta-soa. That session uses the groups in the table.
