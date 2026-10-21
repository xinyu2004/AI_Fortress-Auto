[English](rpmb.md) | [Chinese](../zh/rpmb.md)

# RPMB

![eMMC](../img/en/emmc.svg)

The programming session decides by a version check. RPMB stores the two numbers that check compares: KVN and min SVN. `0x31 F000` carries them inside the Key Manifest. `0x31 F002` carries neither; the image header already does. The figure is that comparison. The region itself is 128 KiB to 4 MiB, with a 256-byte access unit. The eMMC vendor ships the 256-bit HMAC key slot empty. The customer programs it once at end of line, derived from the HUK, so the stored numbers cannot be moved backward. The device does not read the key back.

Every later read or write is one frame: address, data, the write counter inside the device, and HMAC-SHA256. After a successful write the device increments the counter. A rejected frame, a power loss before the device accepts the frame, or a replay of an old frame leaves the counter and the stored value unchanged.

![UFS](../img/en/ufs.svg)

On UFS the same rules apply to the RPMB well-known logical unit, JESD220 identifier `0xC4`. UFS 2.2 and UFS 3.1 allow up to four RPMB regions, each with its own key and its own write counter. KVN and min SVN use one region. `mmc` does not open this unit.

OP-TEE builds the frame and computes the MAC in the secure world. The frame crosses as `OPTEE_RPC_CMD_RPMB` to `tee-supplicant`, a normal-world daemon, not a CA. The supplicant hands the same frame to the driver and returns the response. It cannot compute a valid MAC. The once-only key program uses those same two hops. ca-ota is not on that step. Later, ca-ota calls ta-ota to update the image public key or to activate, and the call stops at ta-ota. If the normal world programs the key first, the TEE cannot use the region afterwards.

KVN increases only when the root public key has verified a new image public key whose KVN is greater. min SVN becomes the package SVN only when the package verifies, the header KVN equals the stored KVN, the header SVN is at least the stored floor, and activate runs. SecOC freshness stays in the TA for this power cycle.

On an eMMC board, Buildroot can install `mmc` from mmc-utils (`BR2_PACKAGE_MMC_UTILS=y`). That command sends MMC ioctls from Linux, including a read of the write counter and the one-time key program. tee-supplicant does not link it. Programming the key with `mmc` before the TEE locks the TEE out of the region.

## When the device has no RPMB

`/data/tee` is present only when OP-TEE is built with `CFG_REE_FS=y`. That option selects the REE filesystem backend, and `tee-supplicant` then stores the objects in this directory. `CFG_REE_FS=n` leaves no `/data/tee`. The files are encrypted with a key from the HUK, and a changed file is detected. Replacing the whole directory with an older copy still verifies, so this store does not block rollback. KVN and min SVN then use another anchor that only increases: a monotonic counter in the SoC, or an NV counter in the HSM. min SVN can also be burned into OTP fuse bits. Those bits are few, and they only record the floor. When no such anchor is available, the two values are not treated as written.

This QEMU virt machine has no eMMC and no UFS device, so `mmc` has nothing to open. The image still selects mmc-utils for a later eMMC board. KVN and min SVN on this image stay in the REE filesystem. Each boot starts again at KVN 1 and min SVN 1. The guest does not check a device write counter.
