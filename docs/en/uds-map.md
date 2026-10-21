[English](uds-map.md) | [Chinese](../zh/uds-map.md)

# UDS and internal commands

The bytes ride on TCP for now. On the QEMU guest, ca-ota listens on port 27100. `scripts/diag_tester.sh` sends the programming path. Pass `aarch32` for the 32-bit guest. Service identifiers follow ISO 14229.

| Service | Role | TA |
| --- | --- | --- |
| `0x10 02` | Enter the programming session | None; CA only |
| `0x27` | Request a seed, then submit the signature | `0x1002`, `0x1003` |
| `0x22` | Read KVN, SVN, and the root-hash fingerprint | `0x1001` |
| `0x31 F000` | Update the image public key | `0x1004` |
| `0x34` | Start download | The signed image header follows as `0x1010` |
| `0x36` | Data chunk | `0x1011` |
| `0x37` | End transfer | Then `0x1012` |
| `0x31 F002` | Activate | `0x1013` |

The `0x36` `blockSequenceCounter` starts at `0x01` and wraps from `0xFF` to `0x00`. Repeating the previous counter is a retransmission. A skip returns NRC `0x24`.

The `FIMG` image header is sent before any data chunk and is signed by the current image private key. It contains SVN, KVN, total length, the package SHA-256, chunk size, chunk count, and the SHA-256 of each chunk. The `FKM1` Key Manifest is signed by the root private key. It carries the new image public key and a higher KVN.
