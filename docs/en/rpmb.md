[English](rpmb.md) | [Chinese](../zh/rpmb.md)

# RPMB, KVN, and min SVN

## KVN and min SVN

KVN, Key Version Number, is the version of the current image public key. A replacement key must be greater, so an older public key cannot return.

SVN, Security Version Number, is the security version of an image. min SVN is the lowest version that may still be activated.

| Value   | When it changes                                                          | If the check fails, or power is lost before the write |
| ------- | ------------------------------------------------------------------------ | ----------------------------------------------------- |
| KVN     | The root public key accepts a new image public key with a higher version | Keep the old public key and the old KVN               |
| min SVN | The whole package verifies and activation writes the package SVN         | Keep the old minimum security version                 |

`0x31 F000` carries the new image public key and its KVN inside the Key Manifest. `0x31 F002` carries neither. The image header already has KVN and SVN. Activation writes min SVN only when the package verifies, the header KVN equals the stored KVN, and the header SVN is at least min SVN. `0x27` uses the public key of the stored KVN. SecOC freshness stays in the TA for this power cycle.

![eMMC](../img/en/emmc.svg)

The right side of the figure is that check. The left side is the partition that holds the two numbers.

## RPMB

RPMB, Replay Protected Memory Block, is that partition. A common size is 128 KiB to 4 MiB, with a 256-byte access unit. The device checks every frame. Linux can carry a frame and cannot change the stored values.

The eMMC vendor ships the 256-bit HMAC key slot empty. The customer programs it once at end of line. ta-ota derives it from the HUK and writes it. The device does not read the key back. If the normal world programs it first, the TEE cannot use the partition afterwards.

Every later read or write is one frame: address, data, the write counter inside the device, and HMAC-SHA256. After a successful write the device increments the counter. A rejected frame, a power loss before the device accepts the frame, or a replay of an old frame leaves the counter and the stored value unchanged. Rewriting the User Data Area does not change KVN or min SVN.

OP-TEE builds the frame and computes the MAC in the secure world. The frame crosses as `OPTEE_RPC_CMD_RPMB` to `tee-supplicant`, a normal-world daemon, not a CA. The supplicant hands the same frame to the driver and returns the response. It cannot compute a valid MAC. The once-only key program uses those same two hops. ca-ota is not on that step. Later, ca-ota calls ta-ota to update the image public key or to activate, and the call stops at ta-ota.

On UFS the same rules apply to the RPMB well-known logical unit, JESD220 identifier `0xC4`. UFS 2.2 and UFS 3.1 allow up to four RPMB regions, each with its own key and its own write counter. KVN and min SVN use one region. `bBootLunEn` only selects which boot LUN runs. `mmc` does not open this unit.

On an eMMC board, Buildroot can install `mmc` from mmc-utils (`BR2_PACKAGE_MMC_UTILS=y`). That command sends MMC ioctls from Linux, including a read of the write counter and the one-time key program. tee-supplicant does not link it.

![RPMB](../img/en/rpmb.svg)

## `/data/tee`, and neither store

**`/data/tee`.** This is the encrypted store for TEE objects that must still be there after reboot, including the SecOC AES key. It exists when `CFG_REE_FS=y`. The file key comes from the HUK, so Linux cannot read the plaintext. A file that has been altered is rejected by the TEE. It does not stop rollback. Root can delete the directory, and an older copy of the whole directory still verifies. If KVN and min SVN live only here, deleting the directory makes ta-ota create the factory state again, KVN 1 and min SVN 1, and the service continues on the old version. `CFG_REE_FS=n` leaves no directory.

**Neither.** The board has no RPMB write counter and no `/data/tee`. Nothing on the device remembers that a higher KVN or a higher min SVN was accepted, so an older image public key and an older package can be accepted again. A key left in an ordinary file can be read and modified, and after reboot the TEE has no protected copy. On that board, OTA anti-rollback and protected key storage are both absent.

This QEMU virt machine has no eMMC and no UFS device, so `mmc` has nothing to open. The image still selects mmc-utils for a later eMMC board. KVN and min SVN on this image stay in the REE filesystem. Each boot starts again at KVN 1 and min SVN 1. The guest does not check a device write counter.
