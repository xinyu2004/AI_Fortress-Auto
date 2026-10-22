#!/bin/bash
# Burn the boot public-key hash into boot/otp.efuse and set burned=1.
# Refuses when burned=1. Set burned=0 by hand to simulate a new chip.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck disable=SC1091
source "$ROOT/boot/efuse.sh"
OTP=$(efuse_path "$ROOT")
PUB=""

while [[ $# -gt 0 ]]; do
	case "$1" in
	--pubkey)
		PUB="$2"
		shift 2
		;;
	--otp)
		OTP="$2"
		shift 2
		;;
	*)
		echo "usage: $0 --pubkey rot_key.pub.pem [--otp path]"
		exit 1
		;;
	esac
done

if [[ -z "$PUB" || ! -f "$PUB" ]]; then
	echo "usage: $0 --pubkey rot_key.pub.pem [--otp path]"
	exit 1
fi
if [[ ! -f "$OTP" ]]; then
	echo "Missing $OTP"
	exit 1
fi

efuse_read "$OTP"
if [[ "$EFUSE_BURNED" == "1" ]]; then
	echo "otp.efuse is already burned (burned=1). The hash cannot be updated."
	echo "Set burned=0 by hand to simulate a new chip. A real eFuse cannot be cleared."
	exit 1
fi

hash=$(rotpk_hash_pem "$PUB")
if [[ ${#hash} -ne 64 ]]; then
	echo "Could not hash the RSA public key. Use: openssl rsa -in rot_key.pem -pubout -out rot_key.pub.pem"
	exit 1
fi
efuse_write "$OTP" 1 "$hash"
echo "Burned the boot public-key hash into $OTP (burned=1)."
