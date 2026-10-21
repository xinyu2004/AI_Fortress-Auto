#!/bin/bash
# Check the aarch64 toolchain, QEMU, and host tools. Does not download sources or install packages.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ROOT/dep-src"

missing=()
command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || missing+=(gcc-aarch64-linux-gnu)
command -v qemu-system-aarch64 >/dev/null 2>&1 || missing+=(qemu-system-arm)
command -v bison >/dev/null 2>&1 || missing+=(bison)
command -v flex >/dev/null 2>&1 || missing+=(flex)
command -v dtc >/dev/null 2>&1 || missing+=(device-tree-compiler)
python3 -c "import elftools" >/dev/null 2>&1 || missing+=(python3-pyelftools)
if ((${#missing[@]})); then
	echo "missing: ${missing[*]}"
	echo "install: sudo apt-get install -y ${missing[*]}"
	exit 1
fi

cp "$ROOT/scripts/aarch64/platform.env" "$ROOT/dep-src/env-aarch64.sh"
echo "wrote dep-src/env-aarch64.sh"
aarch64-linux-gnu-gcc --version | head -1
qemu-system-aarch64 --version | head -1
