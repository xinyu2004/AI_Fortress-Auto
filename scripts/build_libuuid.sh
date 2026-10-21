#!/bin/bash
# Build util-linux libuuid for one CROSS_COMPILE prefix into dep-src/sysroot.
# libteeacl links this library. The script does not install a distro package.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck disable=SC1091
source "$ROOT/versions.env"

CROSS_COMPILE="${1:?usage: build_libuuid.sh <cross-compile-prefix>}"
TRIPLE="${CROSS_COMPILE%-}"
PREFIX="$ROOT/dep-src/sysroot/$TRIPLE"
PC="$PREFIX/lib/pkgconfig/uuid.pc"

if [[ -f "$PC" && -e "$PREFIX/lib/libuuid.so" ]]; then
	echo "libuuid for $TRIPLE is ready"
	exit 0
fi

if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
	echo "${CROSS_COMPILE}gcc not found. Run the matching scripts/prepare_env_*.sh"
	exit 1
fi

CACHE="$ROOT/dep-src/cache"
VER="${LIBUUID_TAG#v}"
SRC="$CACHE/util-linux-$VER"
mkdir -p "$CACHE"

if [[ ! -f "$SRC/configure" ]]; then
	tarball="$CACHE/util-linux-$VER.tar.xz"
	echo "download libuuid $LIBUUID_TAG"
	curl -fsSL "$LIBUUID_URL" -o "$tarball"
	tar -C "$CACHE" -xf "$tarball"
fi

echo "build libuuid for $TRIPLE"
rm -rf "$SRC/build-$TRIPLE"
mkdir -p "$SRC/build-$TRIPLE"
(
	cd "$SRC/build-$TRIPLE"
	"$SRC/configure" \
		--host="$TRIPLE" \
		--prefix="$PREFIX" \
		--disable-all-programs \
		--enable-libuuid \
		--disable-asciidoc \
		--disable-bash-completion \
		CC="${CROSS_COMPILE}gcc" \
		AR="${CROSS_COMPILE}ar"
	make -j"$(nproc)"
	make install
)

PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig" pkg-config --atleast-version=2.34 --exists uuid
echo "installed $PC"
