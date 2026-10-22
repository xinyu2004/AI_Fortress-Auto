

[English](README.md) | [Chinese](README_zh.md)

# AI Fortress Auto

This is a portable OP-TEE handbook for automotive information security, and a QEMU guest that runs SecOC and OTA security before a board is available. On this image AES-128-GCM and P-256 verify run in the OP-TEE PKCS#11 TA. The description is in [docs/en/hsm.md](docs/en/hsm.md).

After reading this you should be able to say how each boot stage passes, what happens when one download chunk is bad, which checks must all hold before min SVN is written, where KVN and min SVN are kept, and which key is used for each operation.

## 1. Overview

![Overview](docs/img/en/overview.svg)

Boot is the next section. This figure is SecOC and OTA security. OTP eFuse holds the hash of the boot public key. The public key itself is in the boot certificate. The application root public key is a different key: only its hash is in OTP. RPMB holds KVN and min SVN, and those two numbers only increase. SecOC freshness stays in ta-soa for this power cycle.

## 2. Secure boot sequence

![Secure boot](docs/img/en/secure-boot.svg)

There is no client application in this chain. One boot public key is burned once. OTP eFuse holds the hash of that public key. The public key itself is in the boot certificate. ROM code compares the hash, and `burned=1` refuses another write.

On a board, the same public key then checks the image that is about to run: BL2, then BL31, then OP-TEE and Linux. A match runs that image. A mismatch stops.

The plain `build_qemu.sh` does not check this key. `build_qemu.sh secure` checks BL2, then BL31, then OP-TEE and U-Boot. That U-Boot then checks the kernel and ramdisk in `fit.itb`. The plain image boots the same FIT and does not require the signature. The hash burned into BL1 is the stand-in for the eFuse field. How to build both images is in [docs/en/trusted-board-boot.md](docs/en/trusted-board-boot.md).

## 3. SecOC

![SecOC](docs/img/en/soa.svg)

This is the in-vehicle case that SECOC covers: two directions, a freshness value, and a secret key that never sits in the client application. The algorithm is AES-128-GCM. The figure is one sequence. The purple side is non-secure: perception and planning. The warm side is secure: ta-soa and the HSM.

- `CMD_SOA_SEAL` (`0x2001`) is the send path on the figure. `CMD_SOA_OPEN` (`0x2002`) is the receive path. The reverse direction uses the same two commands.
- At boot, `ca-ota` installs the AES key into ta-soa with `CMD_SOA_INSTALL_KEY` (`0x2003`). That install is not drawn on the figure.

How to compile and open the guest is in [How to run it](#8-how-to-run-it). SecOC uses two windows. OTA uses those two, then the diagnostic tester.



## 4. RPMB, KVN, and SVN

### KVN and min SVN

**KVN**, Key Version Number, is the version of the current image public key. A replacement key must be greater, so an older public key cannot return.

**SVN**, Security Version Number, is the security version of an image. min SVN is the lowest version that may still be activated.

| Value   | When it changes                                                          | If the check fails, or power is lost before the write |
| ------- | ------------------------------------------------------------------------ | ----------------------------------------------------- |
| KVN     | The root public key accepts a new image public key with a higher version | Keep the old public key and the old KVN               |
| min SVN | The whole package verifies and activation writes the package SVN         | Keep the old minimum security version                 |

`0x31 F000` sends a Key Manifest that already contains the new image public key and its KVN. The root public key verifies that manifest. The manifest KVN must be greater than the stored KVN before the public key is replaced and the new KVN is written. `0x31 F002` sends no version. The image header already carries KVN and SVN. Activation writes min SVN only when the package verifies, the header KVN equals the stored KVN, and the header SVN is at least min SVN. `0x27` checks the unlock signature with the public key of the stored KVN.

![eMMC](docs/img/en/emmc.svg)

The right side of the figure is that check. The left side is the partition that holds the two numbers.

### RPMB

RPMB, Replay Protected Memory Block, is that partition. The device checks every frame. Linux can carry a frame and cannot change the stored values.

The authentication key is a separate 256-bit HMAC key. The eMMC or UFS vendor does not program it. The customer line does, once, derived from the HUK. ta-ota writes it. That frame crosses as `OPTEE_RPC_CMD_RPMB` to `tee-supplicant`, which only carries the frame to the driver. ca-ota is not on this step. The device does not read the key back. If the normal world writes the HMAC key first, the TEE cannot use the partition afterwards, and the board has no counter for KVN and min SVN.

ca-ota calls ta-ota to update the image public key or to activate, and the call stops there. ta-ota builds the frame: an address, the data, the write counter inside the device, and an HMAC-SHA256. Without the key the partition rejects the write. A replay of an old frame is rejected too, because the write counter increases only after a write succeeds. Rewriting the User Data Area does not change either stored number. A rejected write leaves both numbers unchanged.

UFS uses the same two comparisons. KVN and min SVN sit in one RPMB region, the well-known logical unit `0xC4`. UFS 2.2 and UFS 3.1 allow up to four regions, each with its own HMAC key and its own write counter. `bBootLunEn` only selects which boot LUN runs. `mmc` does not open this unit.

On an eMMC board the rootfs can include the `mmc` command from mmc-utils (`BR2_PACKAGE_MMC_UTILS=y`). That command sends MMC ioctls from Linux. It can read the write counter, and it can program the HMAC key. tee-supplicant does not link it. ta-ota still passes the frame to the kernel driver.

![RPMB](docs/img/en/rpmb.svg)

### `/data/tee`, and neither store

**`/data/tee`.** This is the encrypted store for TEE objects that must still be there after reboot. On this image it holds `fortress.ota` (KVN, min SVN, the public-key bytes, and the root hash) and the PKCS#11 token's own storage file. The SecOC AES key is the non-extractable object `fortress-soa` inside that token. The directory exists when `CFG_REE_FS=y`. The file key comes from the HUK, so Linux cannot read the plaintext. A file that has been altered is rejected by the TEE. It does not stop rollback. Root can delete the directory, and an older copy of the whole directory still verifies. If KVN and min SVN live only here, deleting the directory makes ta-ota create the factory state again, KVN 1 and min SVN 1, and the service continues on the old version. `CFG_REE_FS=n` leaves no directory.

**Neither.** The board has no RPMB write counter and no `/data/tee`. Nothing on the device remembers that a higher KVN or a higher min SVN was accepted, so an older image public key and an older package can be accepted again. A key left in an ordinary file can be read and modified, and after reboot the TEE has no protected copy. On that board, OTA anti-rollback and protected key storage are both absent.

This QEMU virt machine has no eMMC and no UFS device, so `mmc` has nothing to open. The image still selects mmc-utils for a later eMMC board. KVN and min SVN on this image stay in the REE filesystem. Each boot starts again at KVN 1 and min SVN 1. The guest does not check a device write counter.

## 5. OTA security: chunk checks, then the decision after download

![OTA security](docs/img/en/ota.svg)

The figure is the sequence: key update, the programming session, the header and each chunk, then activation. The demo does not install the bytes into the running system. The receiver is a buffer.

Activation writes min SVN only when the package verifies, the header KVN equals the stored KVN, and the header SVN is at least min SVN.

The header is signed by the current image private key. It carries the total length, SVN, KVN, the package hash, and the SHA-256 of every chunk. `CMD_IMAGE_UPDATE` (`0x1011`) copies a chunk out of shared memory into the secure world and hashes that copy.

- The hash matches the header entry for that index, and the index is the previous index plus one: accept the chunk.
- The hash does not match: reject only that chunk. The tester retransmits the same index.
- The index skips ahead: reject it. This is UDS NRC `0x24`. Sending the same index again is a retransmission. The UDS `blockSequenceCounter` is one byte. It starts at `0x01`, and after `0xFF` it wraps to `0x00`.
- Power loss in the middle: resume from the last accepted index. min SVN stays unchanged until activation.

`0x22` is `CMD_GET_STATUS` (`0x1001`) and returns version numbers only.

`make -C protocol test` covers these decisions on the host: an old KVN, a bad chunk, a skipped index, retransmission, a lower SVN, power loss before activation (min SVN unchanged), and the UDS counter wrap.

## 6. How each key is used

On this image the SecOC secret key is an object in the OP-TEE PKCS#11 TA. ta-soa keeps a handle for the call. A public key can be read back, and ta-ota also keeps those bytes for the root-hash check. Linux groups are unchanged. The split is in [docs/en/hsm.md](docs/en/hsm.md).


| Key                | Where it lives                                                                                          | What it is used for                                                          |
| ------------------ | ------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| Boot public key    | The hash is in OTP eFuse. The public key is in the boot certificate.                    | Checks BL2, then BL31, then OP-TEE and U-Boot. On the secure QEMU image, U-Boot also checks the kernel and ramdisk in `fit.itb`. |
| HUK                | Another field in the same OTP eFuse.                                                                    | Derives the RPMB HMAC key, and the `/data/tee` key when `CFG_REE_FS=y`.     |
| RPMB HMAC key      | Derived in the TEE from the HUK and written once at customer end of line. The eMMC or UFS vendor does not ship it. | Authenticates each frame of the RPMB region.                                 |
| Root public key    | The private key stays offline. The hash is a different OTP field from the boot public key. Verify uses the PKCS#11 object `fortress-root`. ta-ota keeps the bytes for the hash check. | Verifies a Key Manifest. |
| Image public key   | Verify uses the PKCS#11 object `fortress-image`. KVN stays in ta-ota. | Verifies the image header, each chunk, and the unlock signature. |
| SecOC AES key      | Non-extractable object `fortress-soa` in the PKCS#11 TA. AES-128-GCM. The 16 bytes are not kept in ta-soa. | Encrypts a frame after the freshness check. Freshness stays in ta-soa. |


How the TA reaches the token is in [docs/en/switch-pkcs11.md](docs/en/switch-pkcs11.md).

## 7. Who may open a TA

A CA enters a TA with `TEEC_OpenSession` and `TEEC_LOGIN_GROUP`. The kernel builds the client identity from a Linux group. `libteeacl.so` (`WITH_TEEACL=1`) checks membership and computes the same identity. The build writes it to `dep-src/out/aarch64/optee_client` and `dep-src/out/aarch32/optee_client`. The guest CA links `-lteeacl`.

`fortress-ota` is the group for ca-ota. It is the group that may open ta-ota, and the group that may install the SecOC key into ta-soa. `fortress-perception` and `fortress-planning` are the groups that may seal and open frames on ta-soa. A process outside the group gets no session, so it cannot activate an image or emit a SecOC frame. The calls, and the check the TA does on `gpd.client.identity`, are in [docs/en/session-login.md](docs/en/session-login.md).

## 8. How to run it

The usual target is aarch64. Run one QEMU at a time. Leave the Linux window with Ctrl-a x. Leave the OP-TEE window with Ctrl-]. Sources live outside the git tree in `dep-src/`.

### Compile

```bash
scripts/prepare_sources.sh
scripts/aarch64/prepare_env.sh
scripts/aarch64/build_qemu.sh
```

`prepare_sources.sh` clones the trees pinned in `versions.env`, including mbed TLS. `prepare_env.sh` checks for `bison`, `flex`, and `dtc`. A missing package makes it print the install command and exit. `build_qemu.sh` does not install packages and does not compile QEMU itself. It builds TF-A, U-Boot, OP-TEE, Linux, and Buildroot, then `install_guest.sh` packs ta-ota, ta-soa, perception, planning, and ca-ota into the rootfs. The boot files are in `dep-src/out/aarch64`. If `bl1.bin`, `Image`, or `rootfs.cpio.gz` is missing, `run_SecOC.sh` and `run_OTA.sh` print the missing names and exit. This build leaves the boot check off. After that image exists, `build_qemu.sh secure` checks BL2, BL31, OP-TEE, and U-Boot, and that U-Boot checks the kernel and ramdisk in `fit.itb`. The plain image boots the same FIT. The steps are in [docs/en/trusted-board-boot.md](docs/en/trusted-board-boot.md).

`scripts/aarch64/build.sh` builds OP-TEE OS, optee_client, and the host tests. It does not build the bootable image.

The 32-bit image uses separate Linux, U-Boot, OP-TEE, and TF-A outputs, so it does not replace the aarch64 image:

```bash
scripts/prepare_sources.sh
scripts/aarch32/prepare_env.sh
scripts/aarch32/build_qemu.sh
```

That writes `dep-src/out/aarch32`. It needs `qemu-system-arm`. When `dep-src/` already matches `versions.env`, `prepare_sources.sh` does not download again.

### Run SecOC

Two terminals. SecOC does not use the diagnostic tester.

```bash
scripts/aarch64/run_SecOC.sh
scripts/aarch64/run_tee.sh
```

After Linux boots, ca-ota installs the AES key into ta-soa. perception and planning then exchange a new plaintext every 100 ms.

The 32-bit pair is `scripts/aarch32/run_SecOC.sh` and `scripts/aarch32/run_tee.sh`. Add `secure` to boot the checked image: `scripts/aarch64/run_SecOC.sh secure`.

### Run OTA

Stop the SecOC QEMU first. Two terminals, then the tester:

```bash
scripts/aarch64/run_OTA.sh
scripts/aarch64/run_tee.sh
scripts/diag_tester.sh
```

ca-ota waits. The tester unlocks with `0x27`, then sends the manifest, the ordered chunks, verify, and activate. The 32-bit Linux window is `scripts/aarch32/run_OTA.sh` with `scripts/aarch32/run_tee.sh`. The tester command is `scripts/diag_tester.sh aarch32`.

To move onto your own board, follow [docs/en/porting.md](docs/en/porting.md) and copy `scripts/aarch64/platform.env`. Change `CROSS_COMPILE` and `OPTEE_PLATFORM`. Do not copy the CAs, TAs, or `protocol/`.

## 9. Appendix

- Command numbers and UDS: [docs/en/ca-ta-api.md](docs/en/ca-ta-api.md), [docs/en/uds-map.md](docs/en/uds-map.md)
- Porting: [docs/en/porting.md](docs/en/porting.md)
- Switching the PKCS#11 library: [docs/en/switch-pkcs11.md](docs/en/switch-pkcs11.md)
- QEMU image, first stage: [docs/en/build-qemu.md](docs/en/build-qemu.md)
- Trusted Board Boot on this QEMU: [docs/en/trusted-board-boot.md](docs/en/trusted-board-boot.md)
- Who may open a TA: [docs/en/session-login.md](docs/en/session-login.md)

