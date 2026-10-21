[English](hsm.md) | [Chinese](../zh/hsm.md)

# HSM

An HSM is a module with key slots, a crypto engine, and access control. It may be a separate chip or a security subsystem inside the SoC. A secret key is generated or imported inside the module and marked non-extractable. Callers receive a handle. Encrypt and verify run inside the module, and the plaintext key is not returned.

PKCS#11 is the interface used to reach it: slots, tokens, sessions, objects, and mechanisms. PKCS#11 itself is not an HSM. An automotive application core usually sends requests to an on-chip HSM through a mailbox. This repository uses PKCS#11 so that difference stays behind one library path.

The TEE, here OP-TEE, runs policy: whether KVN may increase, whether SVN is allowed, whether freshness is inside the window, and how a large file is split into chunks. Secret-key operations run in the HSM.

A discrete HSM has a smaller code surface and often adds side-channel protection and an evaluation such as EAL, FIPS 140, or EVITA. A TA inside the TEE is larger, and a defect there can reach a key that lives in Trust World memory. Using both together is the common split.

The OP-TEE PKCS#11 TA on QEMU offers the same interface, and its boundary is still TrustZone. The key leaves TEE memory only when `PKCS11_MODULE` points at a library for separate hardware.

Public keys can be read. The root public-key hash is in OTP. KVN and min SVN are in RPMB, or on another monotonic anchor when the board has no RPMB. SecOC freshness stays in the TA for this power cycle. These values do not occupy key slots.
