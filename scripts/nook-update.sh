#!/bin/sh
# Update MyNook over its USB drive: no adb, no CyanogenMod.
#
#   scripts/nook-update.sh
#
# On the Nook, under MyNook: plug into the Mac and tap "Share books".
# This copies the new system (os/out/uAltRam, from scripts/build-os.sh)
# and its SHA-256 into MyNook-update/ on the drive, then ejects it.
# MyNook takes the drive back, checks the file, installs it and
# restarts into it (reader/platform_fb.c, fb_update).
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="$ROOT/os/out/uAltRam"
VOLUME=/Volumes/MyNOOKcolor

if [ ! -f "$IMG" ]; then
	echo "Build MyNook first: make os" >&2
	exit 1
fi
if [ ! -d "$VOLUME" ]; then
	echo "The Nook's drive isn't on the Mac. Under MyNook, plug in" >&2
	echo "and tap \"Share books\"." >&2
	exit 1
fi

mkdir -p "$VOLUME/MyNook-update"
# -X: no Apple extra files (._uAltRam) on the Nook's FAT drive
cp -X "$IMG" "$VOLUME/MyNook-update/uAltRam"
(cd "$ROOT/os/out" && shasum -a 256 uAltRam) \
	> "$VOLUME/MyNook-update/uAltRam.sha256"
sync
echo "Copied: $(cut -c1-16 "$VOLUME/MyNook-update/uAltRam.sha256")..."
diskutil eject "$VOLUME"
echo "Ejected. The Nook will install the update and restart."
