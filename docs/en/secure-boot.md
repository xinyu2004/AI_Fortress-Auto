[English](secure-boot.md) | [Chinese](../zh/secure-boot.md)

# Secure boot

The boot chain does not go through a client application. One boot public key is burned once. The public key goes into OTP eFuse. ROM code reads that field, and after CLOSED the field cannot be written again. The hash of that public key is burned into the HSM.

The same public key then checks the image that is about to run: BL2, then BL31, then OP-TEE and Linux. A match runs that image. A mismatch stops.

A customer public key is written only while the lifecycle is open. `boot/provision_boot_key.sh` computes the SHA-256 of the uncompressed public-key point and stores it in `boot/otp.state`. That file is the QEMU stand-in for the hash. `boot/lock_boot_key.sh` sets the state to `closed=1`. After that, `otp-provision` returns a locked error. The same rule is what a later TF-A ROTPK hash will use. This repository has not yet produced a TF-A image that contains that hash.
