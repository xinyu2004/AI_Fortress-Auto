#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OTP="${1:-$ROOT/boot/otp.state}"
make -C "$ROOT/boot" >/dev/null
"$ROOT/boot/fortress-tool" otp-lock --otp "$OTP"
