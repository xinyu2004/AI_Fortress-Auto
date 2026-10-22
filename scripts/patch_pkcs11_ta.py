#!/usr/bin/env python3
"""Patch the OP-TEE PKCS#11 TA. dep-src is not committed, so the build applies this again."""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENTRY = ROOT / "dep-src/optee_os/ta/pkcs11/src/entry.c"
TOKEN = ROOT / "dep-src/optee_os/ta/pkcs11/src/pkcs11_token.c"
MARK = "FORTRESS_PKCS11_ALLOW"

ENTRY_OLD = """TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused param_types,
\t\t\t\t    TEE_Param __unused params[4],
\t\t\t\t    void **tee_session)
{
\tstruct pkcs11_client *client = register_client();
"""

ENTRY_NEW = """TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused param_types,
\t\t\t\t    TEE_Param __unused params[4],
\t\t\t\t    void **tee_session)
{
\t/* FORTRESS_PKCS11_ALLOW: only ta-ota and ta-soa may open this token. */
\tTEE_Identity fortress_id;
\tstruct pkcs11_client *client = NULL;
\tstatic const TEE_UUID fortress_ota = {
\t\t0x8f7a1c2e, 0x6b4d, 0x4e91,
\t\t{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x01 }
\t};
\tstatic const TEE_UUID fortress_soa = {
\t\t0x8f7a1c2e, 0x6b4d, 0x4e91,
\t\t{ 0x9c, 0x3a, 0x2d, 0x5e, 0x7f, 0x10, 0xa0, 0x02 }
\t};

\tif (TEE_GetPropertyAsIdentity(TEE_PROPSET_CURRENT_CLIENT,
\t\t\t\t      "gpd.client.identity",
\t\t\t\t      &fortress_id) != TEE_SUCCESS ||
\t    fortress_id.login != TEE_LOGIN_TRUSTED_APP ||
\t    (TEE_MemCompare(&fortress_id.uuid, &fortress_ota,
\t\t\t    sizeof(fortress_ota)) &&
\t     TEE_MemCompare(&fortress_id.uuid, &fortress_soa,
\t\t\t    sizeof(fortress_soa)))) {
\t\tEMSG("PKCS11 session refused");
\t\treturn TEE_ERROR_ACCESS_DENIED;
\t}

\tclient = register_client();
"""

TOKEN_OLD = "\t/* Verify authentication if token is already initialized */\n"
TOKEN_NEW = (
    "\t/* FORTRESS_PKCS11_ALLOW: a second init must not erase keys. */\n"
    "\tif (token->db_main->flags & PKCS11_CKFT_TOKEN_INITIALIZED)\n"
    "\t\treturn PKCS11_CKR_OK;\n\n"
    "\t/* Verify authentication if token is already initialized */\n"
)


def patch(path: pathlib.Path, old: str, new: str) -> None:
    text = path.read_text()
    if MARK in text:
        return
    if old not in text:
        sys.exit(f"anchor missing in {path}")
    path.write_text(text.replace(old, new, 1))


def main() -> None:
    if not ENTRY.is_file() or not TOKEN.is_file():
        sys.exit("PKCS#11 TA sources are missing. Run scripts/prepare_sources.sh")
    patch(ENTRY, ENTRY_OLD, ENTRY_NEW)
    patch(TOKEN, TOKEN_OLD, TOKEN_NEW)


if __name__ == "__main__":
    main()
