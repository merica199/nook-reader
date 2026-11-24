#!/bin/sh
# Run the reader on the Nook under CyanogenMod, for a quick test
# without restarting into MyNook.
#
#   scripts/nook-run.sh            build, push, run
#   MR_ROTATE=ccw scripts/...      if the picture comes out upside down
#
# Needs Docker running: the build happens in the GCC container
# (scripts/build-toolchain.sh).
#
# Pauses Android ("adb shell stop" stops the screen compositor and
# apps, so the reader gets the screen and the touchscreen to itself),
# runs the reader on the books, and brings Android back ("adb shell
# start") when it quits. Under Android the power button only turns
# the screen off; hold it and choose "Power off" to quit.
#
# It shares /data/mynook with MyNook: the same saved places, settings
# and dictionary. The program itself goes in /data/local/tmp.
set -e

DIR=/data/mynook
BIN=/data/local/tmp/mynook-reader
BOOKS="/storage/sdcard1/My Files/Books"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
make -C "$ROOT/reader" nook

echo "Waiting for the Nook (plug it in, USB debugging on)..."
adb wait-for-device
adb shell mkdir -p "$DIR/dict"
adb push "$ROOT/reader/reader-nook" "$BIN"
adb shell chmod 755 "$BIN"

# adb on Android 4.3 doesn't pass back exit codes, so check output.
sh_ok() {
	[ "$(adb shell "$1 && echo ok" | tr -d '\r' | tail -1)" = ok ]
}

# Saved places from before /data/mynook move over once.
OLD=/data/local/tmp/myreader
if sh_ok "[ -f $OLD/state ] && [ ! -f $DIR/state ]"; then
	adb shell "mv $OLD/state $DIR/state"
fi
if sh_ok "[ -f $OLD/dict/dict.dat ] && [ ! -f $DIR/dict/dict.dat ]"; then
	adb shell "mv $OLD/dict/dict.idx $OLD/dict/dict.dat $DIR/dict/"
fi

# The dictionary is 180 MB, so it's only pushed when its size on the
# Nook differs from the Mac's copy.
DICT="$ROOT/dict"
if [ -f "$DICT/dict.dat" ]; then
	here=$(wc -c < "$DICT/dict.dat" | tr -d ' ')
	there=$(adb shell "busybox wc -c < $DIR/dict/dict.dat 2>/dev/null" \
		| tr -d '\r ')
	if [ "$here" != "$there" ]; then
		echo "Copying the dictionary (about 30 seconds)..."
		adb push "$DICT/dict.idx" "$DIR/dict/dict.idx"
		adb push "$DICT/dict.dat" "$DIR/dict/dict.dat"
	fi
fi

# Whatever happens from here (quit, error, Ctrl-C), stop the reader
# if it's still running and give the Nook back to Android.
cleanup() {
	adb shell "kill \$(cat $DIR/pid) 2>/dev/null; rm -f $DIR/pid" \
		|| true
	adb shell start || true
	echo "Android restarted."
}
trap cleanup EXIT INT TERM

# If the Nook shares its books as a USB drive when plugged in (this
# one has a small script that does that on every plug-in), Android
# unmounts them while the Mac has them, so the reader would find no
# books. Take them back: eject on the Mac (a clean unmount), then turn
# USB storage off through MountService, which remounts it on the
# Nook. Must happen before "stop": MountService dies with Android.
# Harmless on a Nook without such a script.
VOLUME=/Volumes/MyNOOKcolor
echo "Taking the books back from the Mac..."
i=0
while [ ! -d "$VOLUME" ] && [ $i -lt 30 ]; do
	sleep 1	# give the watcher time to finish its handoff
	i=$((i + 1))
done
if [ -d "$VOLUME" ]; then
	diskutil eject "$VOLUME"
fi
adb shell service call mount 4 i32 0 >/dev/null

i=0
until sh_ok "[ -d '$BOOKS' ]"; do
	i=$((i + 1))
	if [ $i -ge 20 ]; then
		echo "Books folder never came back on the Nook: $BOOKS"
		exit 1
	fi
	sleep 1
done
echo "Books are mounted on the Nook."

adb shell stop
adb shell "cd $DIR && MR_ROTATE=${MR_ROTATE:-cw} MR_DICT=$DIR/dict \
	TZ=EST5EDT,M3.2.0,M11.1.0 $BIN '$BOOKS' $DIR/state & \
	echo \$! > $DIR/pid; wait"
