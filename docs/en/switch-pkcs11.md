[English](switch-pkcs11.md) | [Chinese](../zh/switch-pkcs11.md)

# Wiring the PKCS#11 token

The application commands do not change. A CA still opens ta-ota or ta-soa. It does not open the token.

On this QEMU image the token is the OP-TEE PKCS#11 TA. ta-ota and ta-soa call it from the secure world. They do not call `TEE_ALG_AES_GCM` or `TEE_ALG_ECDSA_P256`. `config/hsm.env` is not read. The split is in [hsm.md](hsm.md).

The client code lives next to the two TAs. It opens one session, initializes the token on first use, logs in, and then creates or replaces objects by label. The 16-byte AES key is wiped from the TA stack after the object is created. ta-soa no longer stores that key in the `fortress.soa` persistent object. ta-ota keeps KVN, min SVN, and the root public-key hash in `fortress.ota`.

The guest rootfs must contain `fd02c9da-306c-48c7-a49c-bbd827ae86ee.ta`.

`libckteec.so` stays a normal-world tool. `scripts/check_pkcs11.sh` and `pkcs11-tool` use it to look at a token. A normal-world `pkcs11-tool` session is refused. The SecOC and OTA runs are the check that the token works.

```bash
pkcs11-tool --module <so> -I
pkcs11-tool --module <so> -L
pkcs11-tool --module <so> -M
```

`-M` must include ECDSA and AES-GCM. This listing is how a board token is inspected. It is not the path ta-soa uses.

A later board that has a PKCS#11 token inside the secure world keeps these TA calls and changes the token. A chip that only has SHE key slots, and no secure-world PKCS#11 token, needs a HAL with two operations: P-256 verify and AES-128-GCM. The TA still decides KVN, min SVN, the chunk index, and freshness.
