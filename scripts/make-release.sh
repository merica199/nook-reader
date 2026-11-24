#!/bin/sh
# Make the files for a GitHub release of the version in VERSION, in
# os/out/release/<version>/:
#
#   MyNook-update-<v>.zip     the MyNook-update folder, for updating
#                             over the Nook's USB drive
#   MyNook-install-<v>.zip    a first install without building:
#                             installer, system image, dictionary
#   MyNook-<v>-third-party-source.tar.gz
#                             BusyBox (with Buildroot's patches) and
#                             musl, which are inside the image, with
#                             Buildroot's license manifest
#   mupdf-1.27.2-source.tar.gz
#                             MuPDF, as downloaded from mupdf.com
#   SHA256SUMS.txt            the hashes of all of the above
#
# The working tree must be clean, so the release matches a commit:
# tag that commit v<version>. Needs everything make install needs.
#
#   scripts/make-release.sh
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
V=$(cat VERSION)
OUT="$ROOT/os/out/release/$V"

if [ -n "$(git status --porcelain)" ]; then
	echo "Commit everything first: a release must match a commit." >&2
	exit 1
fi
if [ ! -f dict/dict.dat ]; then
	echo "Build the dictionary first: make dict" >&2
	exit 1
fi

echo "Building MyNook $V..."
make os >/dev/null

rm -rf "$OUT"
mkdir -p "$OUT"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

# The update: what the user guide says to put on the Nook's drive.
mkdir -p "$STAGE/update/MyNook-update"
cp os/out/uAltRam "$STAGE/update/MyNook-update/uAltRam"
(cd "$STAGE/update/MyNook-update" && shasum -a 256 uAltRam \
	> uAltRam.sha256)
(cd "$STAGE/update" && zip -q -r -X "$OUT/MyNook-update-$V.zip" \
	MyNook-update)

# The first install: the installer, laid out the way it expects.
I="$STAGE/install/MyNook-install-$V"
mkdir -p "$I/scripts" "$I/os/out" "$I/dict"
cp scripts/nook-install.sh "$I/scripts/"
cp os/out/uAltRam "$I/os/out/"
cp dict/dict.idx dict/dict.dat "$I/dict/"
cp LICENSE "$I/LICENSE"
cat > "$I/INSTALL.txt" <<EOF
MyNook $V: first install

For the Barnes & Noble Nook Color (BNRV200) running CyanogenMod
10.2, with root access over adb, only. Not for any other Nook.

1. On the Nook, under CyanogenMod: turn on USB debugging, and set
   Root access to "ADB only" (Settings, Developer options).
2. Plug it into the computer, and check that "adb devices" lists it.
3. In this folder, run:   sh scripts/nook-install.sh
   It refuses anything that isn't a Nook Color set up as above. It
   backs up the boot partition to os/out/p1-backup.img first.
4. To start MyNook: power off, then hold n while powering on, and
   choose "Internal eMMC Alternate". To have it start without the
   menu:   sh scripts/nook-install.sh default

After this, updates need no computer setup: see the user guide,
docs/user-guide.md in the source, under "Updating MyNook".

The dictionary (dict/) is built from English Wiktionary, via
kaikki.org's wiktextract, and is licensed CC BY-SA 4.0, like its
source. Everything else: GNU AGPL v3 (LICENSE).
EOF
(cd "$STAGE/install" && zip -q -r -X "$OUT/MyNook-install-$V.zip" \
	"MyNook-install-$V")

# Third-party source for what's inside the image. Buildroot's
# legal-info gathers each package's source, patches and license.
# GCC's runtime libraries (GCC Runtime Library Exception) and the
# Linux headers (syscall note) need no source shipped; the manifest
# lists them with where they come from.
docker run --rm -v mynook-br:/br -v "$STAGE:/stage" mynook-br sh -c "
	set -e
	cd /br/buildroot
	make legal-info >/dev/null 2>&1
	L=output/legal-info
	mkdir -p /stage/src/MyNook-$V-third-party-source/sources
	cp -r \$L/README \$L/manifest.csv \$L/buildroot.config \
		\$L/licenses /stage/src/MyNook-$V-third-party-source/
	cp -r \$L/sources/busybox-* \$L/sources/musl-* \
		/stage/src/MyNook-$V-third-party-source/sources/
	"
(cd "$STAGE/src" && tar -czf \
	"$OUT/MyNook-$V-third-party-source.tar.gz" \
	"MyNook-$V-third-party-source")
cp vendor/mupdf-1.27.2-source.tar.gz "$OUT/"

(cd "$OUT" && shasum -a 256 *.zip *.tar.gz > SHA256SUMS.txt)
ls -l "$OUT"
echo "Commit: $(git rev-parse --short HEAD); tag it v$V."
