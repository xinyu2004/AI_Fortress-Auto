[English](switch-pkcs11.md) | [Chinese](../zh/switch-pkcs11.md)

# Switching to a vendor PKCS#11 library

The application commands do not change. Edit `config/hsm.env`:

- `PKCS11_MODULE`: absolute path of the vendor `.so`
- `PKCS11_SLOT`: the slot number shown by `-L` below
- `PKCS11_TOKEN_LABEL` and `PKCS11_PIN`: used to open the token

Reopen the CA sessions, then run `scripts/check_pkcs11.sh`.

Tool reference: [pkcs11-tool manual](https://man.archlinux.org/man/pkcs11-tool.1.en). A real device listing can be compared with [YubiHSM 2](https://docs.yubico.com/hardware/yubihsm-2/hsm-2-user-guide/hsm2-opensc-pkcs11.html). That library is `yubihsm_pkcs11.so`. After login you should see slot 0 and the token name `YubiHSM`.

```bash
pkcs11-tool --module <so> -I
pkcs11-tool --module <so> -L
pkcs11-tool --module <so> -M
pkcs11-tool --module <so> --slot <slot> --login --pin <PIN> -O
pkcs11-tool --module <so> --slot <slot> --login --pin <PIN> --generate-random 16
```

`-M` must include ECDSA-SHA256 and AES-GCM. In `-O`, a secret key should appear as a non-extractable object.

A chip that only has SHE key slots, and no PKCS#11 shared library, cannot be attached by changing the path. That hardware needs a HAL adapter for verify, AES-GCM, random numbers, and the monotonic counter.
