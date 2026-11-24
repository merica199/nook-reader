#!/bin/sh
# Build the Nook's GCC cross-compiler with Buildroot, inside Docker.
#
#   scripts/build-toolchain.sh
#
# Run once (about 11 minutes on a 10-core Mac), after scripts/fetch.sh.
# The result lives in the Docker volume "mynook-br", at
# /br/buildroot/output/host/bin/arm-buildroot-linux-musleabihf-gcc.
# The volume is case-sensitive Linux storage, which Buildroot needs
# and the Mac's disk isn't. Buildroot doesn't run on macOS at all,
# hence the container (os/Dockerfile).
set -e

BR=buildroot-2026.08.2
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

docker build -t mynook-br "$ROOT/os"
docker volume create mynook-br >/dev/null
# A new volume belongs to root; hand it to the build user.
docker run --rm -u 0 -v mynook-br:/br mynook-br chown builder: /br

docker run --rm -v mynook-br:/br -v "$ROOT:/src" mynook-br sh -c "
	set -e
	if [ ! -d /br/buildroot ]; then
		tar -C /br -xf /src/vendor/$BR.tar.xz
		mv /br/$BR /br/buildroot
	fi
	cd /br/buildroot
	cp /src/os/mynook_defconfig configs/mynook_defconfig
	make mynook_defconfig
	make -j\$(nproc) toolchain
	output/host/bin/arm-buildroot-linux-musleabihf-gcc --version
	"
