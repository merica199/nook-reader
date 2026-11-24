#!/bin/sh
# Build MyNook: BusyBox, MyNook's start-up files (os/overlay) and the
# reader, packed into one archive for the bootloader.
#
#   scripts/build-os.sh
#
# Needs the toolchain (scripts/build-toolchain.sh) and the reader's
# Nook build (make -C reader nook). Writes os/out/uAltRam, which
# scripts/nook-install.sh puts on the Nook's boot partition.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/os/out"

if [ ! -f "$ROOT/reader/reader-nook" ]; then
	echo "Build the reader first: make -C reader nook" >&2
	exit 1
fi

# The reader goes in through a second overlay, so os/overlay holds
# only files kept in the repo.
mkdir -p "$OUT/overlay/usr/bin"
cp "$ROOT/reader/reader-nook" "$OUT/overlay/usr/bin/reader"

docker run --rm -v mynook-br:/br -v "$ROOT:/src" mynook-br sh -c "
	set -e
	cd /br/buildroot
	cp /src/os/mynook_defconfig configs/mynook_defconfig
	make mynook_defconfig >/dev/null
	make -j\$(nproc)
	cp output/images/rootfs.cpio.uboot /src/os/out/uAltRam
	"
ls -l "$OUT/uAltRam"
