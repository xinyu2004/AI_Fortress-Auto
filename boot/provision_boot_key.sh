#!/bin/bash
# While OPEN, write the customer root public-key hash. Refuse the write when CLOSED.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OTP="$ROOT/boot/otp.state"
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
		echo "usage: $0 --pubkey customer.pem [--otp path]"
		exit 1
		;;
	esac
done

if [[ -z "$PUB" ]]; then
	echo "usage: $0 --pubkey customer.pem [--otp path]"
	exit 1
fi

make -C "$ROOT/boot" >/dev/null
"$ROOT/boot/fortress-tool" otp-provision --pub "$PUB" --otp "$OTP"
