#!/bin/bash
# Check the aarch32 toolchain and QEMU. Does not download sources or install packages.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ROOT/dep-src"

missing=()
command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1 || missing+=(gcc-arm-linux-gnueabihf)
command -v qemu-system-arm >/dev/null 2>&1 || missing+=(qemu-system-arm)
python3 -c "import elftools" >/dev/null 2>&1 || missing+=(python3-pyelftools)
if ((${#missing[@]})); then
	echo "missing: ${missing[*]}"
	echo "install: sudo apt-get install -y ${missing[*]}"
	exit 1
fi

cp "$ROOT/scripts/aarch32/platform.env" "$ROOT/dep-src/env-aarch32.sh"
echo "wrote dep-src/env-aarch32.sh"
arm-linux-gnueabihf-gcc --version | head -1
qemu-system-arm --version | head -1
