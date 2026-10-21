#!/bin/bash
# Check the PKCS#11 library with -I, -L, -M, -O, and a random read. Fail if a mechanism is missing.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck disable=SC1091
source "$ROOT/config/hsm.env"

if ! command -v pkcs11-tool >/dev/null 2>&1; then
	echo "pkcs11-tool is missing. Install opensc, then run this again."
	exit 1
fi

echo "module: $PKCS11_MODULE"
pkcs11-tool --module "$PKCS11_MODULE" -I
echo
echo "slots:"
pkcs11-tool --module "$PKCS11_MODULE" -L
echo
echo "mechanisms:"
mechs=$(pkcs11-tool --module "$PKCS11_MODULE" -M)
printf '%s\n' "$mechs"
echo "$mechs" | grep -E -q 'ECDSA.*SHA256|SHA256.*ECDSA|CKM_ECDSA_SHA256' || {
	echo "missing ECDSA-SHA256"
	exit 1
}
echo "$mechs" | grep -q 'AES-GCM' || {
	echo "missing AES-GCM"
	exit 1
}

if [[ "${PKCS11_PIN:-}" != "" ]]; then
	echo
	echo "objects:"
	pkcs11-tool --module "$PKCS11_MODULE" --slot "$PKCS11_SLOT" --login --pin "$PKCS11_PIN" -O || true
	echo
	echo "random:"
	pkcs11-tool --module "$PKCS11_MODULE" --slot "$PKCS11_SLOT" --login --pin "$PKCS11_PIN" --generate-random 16 | wc -c
fi
echo "PKCS#11 check done"
