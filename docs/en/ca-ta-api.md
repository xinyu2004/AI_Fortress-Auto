[English](ca-ta-api.md) | [Chinese](../zh/ca-ta-api.md)

# CA and TA commands

A client application uses `TEEC_OpenSession` and `TEEC_InvokeCommand`. The session login is `TEEC_LOGIN_GROUP`. Which Linux group may open each TA is in [session-login.md](session-login.md). Small integers use `TEEC_VALUE`. Data blocks use shared memory. The TA copies shared memory into the secure world before it hashes or verifies.

Header: [protocol/include/fortress/ta_ids.h](../../protocol/include/fortress/ta_ids.h).

## ta-ota

| Command | Value | Caller | Action |
| --- | --- | --- | --- |
| `CMD_GET_STATUS` | `0x1001` | ca-ota, UDS `0x22` | Return KVN and min SVN |
| `CMD_GET_SEED` | `0x1002` | ca-ota, UDS `0x27` | OP-TEE random; the seed stays in the TA session |
| `CMD_UNLOCK` | `0x1003` | ca-ota, UDS `0x27` | The current image public key verifies the seed signature |
| `CMD_UPDATE_KEY_MANIFEST` | `0x1004` | ca-ota, UDS `0x31 F000` | Root public key checks the manifest; write only if KVN is greater |
| `CMD_IMAGE_BEGIN` | `0x1010` | ca-ota | Verify the image-header signature; check KVN and the SVN floor |
| `CMD_IMAGE_UPDATE` | `0x1011` | ca-ota, UDS `0x36` | Chunk hash; accept only the next index |
| `CMD_IMAGE_VERIFY` | `0x1012` | ca-ota, after UDS `0x37` | Package hash and image-key verify; SVN is not written yet |
| `CMD_IMAGE_ACTIVATE` | `0x1013` | ca-ota, UDS `0x31 F002` | Write min SVN. This image uses the REE filesystem. A board writes RPMB |

## ta-soa

| Command | Value | Caller | Action |
| --- | --- | --- | --- |
| `CMD_SOA_SEAL` | `0x2001` | perception or planning | Freshness check, then AES-GCM in ta-soa. The send counter increases only after encrypt succeeds |
| `CMD_SOA_OPEN` | `0x2002` | the peer | Check the receive counter, then AES-GCM open. Reject an old frame |
| `CMD_SOA_INSTALL_KEY` | `0x2003` | ca-ota, at boot | Install the AES key into ta-soa |

On failure the counters stay as they are. Results use the GlobalPlatform `TEEC_Result`: a bad signature or GCM tag is `TEE_ERROR_MAC_INVALID` or `TEE_ERROR_SIGNATURE_INVALID`; a version or sequence failure is `TEE_ERROR_SECURITY`.

The host `protocol/` library applies the same decisions. Integer codes are in `fortress/status.h`. The AArch64 QEMU image carries ta-ota and ta-soa. `make -C protocol test` covers the decisions.
