[English](../en/switch-pkcs11.md) | 中文

# 换成厂商 PKCS#11 库

业务命令不变。改 `config/hsm.env`：

- `PKCS11_MODULE`：厂商 `.so` 的绝对路径
- `PKCS11_SLOT`：下面 `-L` 看到的槽号
- `PKCS11_TOKEN_LABEL` 与 `PKCS11_PIN`：打开 token

改完重新打开 CA 会话，再执行 `scripts/check_pkcs11.sh`。

工具说明：[pkcs11-tool 手册](https://man.archlinux.org/man/pkcs11-tool.1.en)。真机输出可以对照 [YubiHSM 2](https://docs.yubico.com/hardware/yubihsm-2/hsm-2-user-guide/hsm2-opensc-pkcs11.html)，那里的库是 `yubihsm_pkcs11.so`，登录后能看到槽 0 和 token 名 `YubiHSM`。

```bash
pkcs11-tool --module <so> -I
pkcs11-tool --module <so> -L
pkcs11-tool --module <so> -M
pkcs11-tool --module <so> --slot <槽> --login --pin <PIN> -O
pkcs11-tool --module <so> --slot <槽> --login --pin <PIN> --generate-random 16
```

`-M` 的结果里要有 ECDSA-SHA256 和 AES-GCM。`-O` 里的秘密密钥应显示为不可导出对象。

只有 SHE 密钥槽、没有 PKCS#11 动态库的芯片，不能靠改路径接上。那种硬件要在 HAL 里按验签、AES-GCM、随机数、单调计数这几项做适配。
