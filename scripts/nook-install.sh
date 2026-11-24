#!/bin/sh
# Install MyNook on the Nook, beside CyanogenMod, over adb.
#
#   scripts/nook-install.sh            install or update
#   scripts/nook-install.sh remove     take MyNook off again
#
# Run it while CyanogenMod is up and the Nook is plugged in (or, once
# MyNook's own adb works, while MyNook is up). It needs
# os/out/uAltRam (scripts/build-os.sh) and dict/ (the dictionary).
#
# What it changes:
#   - the boot partition (p1) gets two new files, uAltImg (a copy of
#     CyanogenMod's kernel) and uAltRam (MyNook). CyanogenMod's own
#     files there are left alone. The bootloader's menu calls this
#     pair "Alternate".
#   - /data/mynook gets the dictionary, the saved places and settings
#     from testing under Android (/data/local/tmp/myreader), and a
#     copy of CyanogenMod's adbd, so adb works under MyNook too.
#
# Before the first install, the whole boot partition is copied to
# os/out/p1-backup.img on the Mac.
#
# To start MyNook: power off, then power on holding the n button until
# the boot menu shows, and choose "Internal eMMC Alternate". Without
# the menu the Nook starts CyanogenMod, as before.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/os/out"
P1=/data/local/tmp/p1
DATA=/data/mynook

adb wait-for-device
# adb on Android 4.3 doesn't pass exit codes back, so check output.
sh_ok() {
	[ "$(adb shell "$1 && echo ok" | tr -d '\r' | tail -1)" = ok ]
}

# Only the Nook Color ("encore"), rooted, with a bootloader that has
# the alternate slot. Anything else: stop before touching anything.
refuse() {
	echo "Not installing: $1" >&2
	echo "MyNook supports only the Nook Color (BNRV200) running" >&2
	echo "CyanogenMod 10.2 with root over adb. See README.md." >&2
	exit 1
}
device=$(adb shell getprop ro.product.device | tr -d '\r')
[ "$device" = encore ] || \
	refuse "this is \"$device\", not a Nook Color (\"encore\")"
sh_ok "id | busybox grep -q 'uid=0'" || refuse "adb isn't running as root"

mount_p1() {
	adb shell "mkdir -p $P1"
	sh_ok "mount -t vfat /dev/block/mmcblk0p1 $P1" || {
		echo "Couldn't mount the boot partition" >&2
		exit 1
	}
}

unmount_p1() {
	adb shell "sync; umount $P1; rmdir $P1"
}

if [ "$1" = remove ]; then
	mount_p1
	adb shell "rm -f $P1/uAltImg $P1/uAltRam"
	unmount_p1
	echo "MyNook removed from the boot partition."
	echo "(/data/mynook, with the dictionary and saved places, stays.)"
	exit 0
fi

if [ ! -f "$OUT/uAltRam" ]; then
	echo "Build MyNook first: scripts/build-os.sh" >&2
	exit 1
fi

# A copy of the boot partition, once, before anything is written.
if [ ! -f "$OUT/p1-backup.img" ]; then
	echo "Backing up the boot partition..."
	adb shell "dd if=/dev/block/mmcblk0p1 of=/data/local/tmp/p1.img"
	adb pull /data/local/tmp/p1.img "$OUT/p1-backup.img"
	adb shell "rm /data/local/tmp/p1.img"
fi

mount_p1
# CyanogenMod's bootloader loads uAltImg/uAltRam; others may not.
if ! sh_ok "busybox grep -q uAltImg $P1/u-boot.bin"; then
	unmount_p1
	refuse "this bootloader has no alternate boot slot (uAltImg)"
fi
echo "Installing MyNook on the boot partition..."
# New names first, then renamed: a pulled cable never leaves a
# half-written file under the name the bootloader loads.
adb push "$OUT/uAltRam" "$P1/uAltRam.new"
adb shell "cp $P1/uImage $P1/uAltImg.new"
adb shell "mv $P1/uAltRam.new $P1/uAltRam; mv $P1/uAltImg.new $P1/uAltImg"
adb shell "ls -l $P1"
unmount_p1

# MyNook's own files. Saved places from testing under Android move
# over once.
adb shell "mkdir -p $DATA/dict"
# adb for MyNook: CyanogenMod's own adbd (statically linked), copied
# from its startup archive, which is only reachable while it runs.
adb shell "cp /sbin/adbd $DATA/adbd && chmod 755 $DATA/adbd"
OLD=/data/local/tmp/myreader
if sh_ok "[ -f $OLD/state ] && [ ! -f $DATA/state ]"; then
	adb shell "mv $OLD/state $DATA/state"
fi
if sh_ok "[ -f $OLD/dict/dict.dat ] && [ ! -f $DATA/dict/dict.dat ]"; then
	adb shell "mv $OLD/dict/dict.idx $OLD/dict/dict.dat $DATA/dict/"
fi
here=$(wc -c < "$ROOT/dict/dict.dat" | tr -d ' ')
there=$(adb shell "busybox wc -c < $DATA/dict/dict.dat 2>/dev/null" \
	| tr -d '\r ')
if [ "$here" != "$there" ]; then
	echo "Copying the dictionary (about 30 seconds)..."
	adb push "$ROOT/dict/dict.idx" "$DATA/dict/dict.idx"
	adb push "$ROOT/dict/dict.dat" "$DATA/dict/dict.dat"
fi

echo
echo "Installed. To start MyNook: power off, then power on holding"
echo "the n button until the boot menu shows, and choose"
echo "\"Internal eMMC Alternate\"."
