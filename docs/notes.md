# Development notes

What's been checked, what hasn't, and what was learned on the way.

## Verified on the Nook

Nook Color, CyanogenMod 10.2.1 (Android 4.3.1), kernel 3.0.8.

- **2026-11-23, read over adb:**
  - framebuffer: RGB565, 1024x600, stride 2048;
  - touch: `event2`, multitouch protocol B;
  - buttons: `event0`, `event1`;
  - adb runs as root.
- **2026-11-24, the reader under Android (`make run`):**
  - the picture is the right way up (`cw`);
  - touch lands where the finger is;
  - the books load;
  - the backlight works (`lcd-backlight`, max 255);
  - the dictionary panel works;
  - text sizes up to 28 pt.
- **2026-11-24, read over adb, for MyNook:**
  - fuel gauge (`max17042-0`) and real-time clock (`twl_rtc`, UTC) present;
  - `/sys/power/state` takes `mem`, with wake locks;
  - the USB gadget has two drive slots;
  - the boot partition holds `MLO`, `u-boot.bin`, `uImage`, `uRamdisk`, `uRecImg`, `uRecRam`;
  - the bootloader's script has the `altboot` slot (`uAltImg` + `uAltRam`) and the boot counter (section 2 of how-it-works.md);
  - `/rom/devconf/BootCnt` reads 0.
- **2026-11-24, MyNook installed:**
  - boot partition backed up first;
  - `uAltRam` on the Nook matches the build by SHA-256;
  - `uAltImg` is identical to `uImage`.

- **2026-11-24, MyNook on the Nook:**
  - n opens the boot menu; "Internal eMMC Alternate" boots MyNook;
  - the reader starts into the book, in color; touch works after the slot fix (below), and the buttons;
  - USB shows up on the Mac, with adb, after the `android_adb` fix (below);
  - with `/rom/u-boot.altboot` set to `1`, a plain power-on starts MyNook;
  - the boot menu's "Internal eMMC Normal" still starts CyanogenMod with the default switched;
  - **the self-update**: 0.1.0 copied over the shared drive, installed, and restarted into (Settings showed the new version);
  - sharing the books over USB under MyNook: the drive appeared on the Mac.

## Not yet verified

- **`adb shell` under MyNook.** Copying files works; the shell was refused, most likely because mdev made `/dev/ptmx` root-only and adbd runs as `shell`. `/etc/mdev.conf` opens it in 0.2.0.
- **Sleep** through early suspend with no Android running, and the power button waking it.
- **USB sharing** under MyNook: eject detection and remounting (the drive appearing on the Mac is verified).
- **Plugging in while asleep** waking the screen: whether this kernel wakes from deep sleep on a charger. If it doesn't, the screen lights the next time something else wakes it.
- **A wall charger** under MyNook: charges, lights the lock screen, and doesn't ask about connecting.
- **Power off** (SIGUSR2 to BusyBox init), and restart landing in CyanogenMod.
- **Speed** on the Nook: opening books, the dictionary lookup, drawing covers, and color frames (three table lookups a pixel instead of one).
- **Color** on the Nook's own screen (RGB565 packing of real colors; only gray had been shown on it).

## Lessons

- **Multitouch slots aren't always 0.** Under MyNook the Nook's touch driver put the first finger in slot 1; the reader only followed slot 0 and dropped every touch. It now follows whichever slot the first finger lands in, and reads the driver's current slot when it opens the device.
- **Android's USB gadget waits for adbd.** With `adb` among its functions, this kernel keeps the whole USB connection off until adbd opens `/dev/android_adb`. Without Android's settings service, adbd drops to the `shell` user, and `mdev` makes that file root-only, so the computer saw nothing at all. MyNook opens the file to it, and lists adb only when adbd is installed.
- **adbd lives in CyanogenMod's startup archive** (`/sbin/adbd`, statically linked), not on `/system`. The installer copies it to `/data/mynook`.
- **If adb stops seeing the Nook** while it's plugged in under CyanogenMod, restart the Nook. On 2026-11-24 the USB connection got stuck half set up; replugging and toggling USB debugging didn't help, and a restart did.

- **Lay out one chapter at a time.**
  - Drawing a page takes 0.3 to 0.5 ms (median), 20 to 50 ms at worst (pictures).
  - Laying out a whole book takes 67 to 648 ms, so books open by chapter: about 5 ms.
  - `fz_count_pages` forces the whole layout and is never called.
- **MuPDF bookmarks can't be saved:** on an EPUB, `fz_make_bookmark` returns a memory address. Places are saved as chapter, page and chapter length instead.
- **`fz_clear_pixmap_rect_with_value` does nothing on a gray pixmap** in 1.27.2 (tested). `draw.c` fills rectangles itself.
- **MuPDF's default stylesheet has `@page { margin: 3em 2em }`.** Those margins grow with the text, to 124 px a side at 28 pt. The reader's own CSS turns them off with `!important` and sets fixed margins.
- **GCC's `-Wclobbered` finds what clang can't.** After `fz_try`'s `longjmp`, a local changed since the `setjmp` is undefined (C11 7.13.2.1). Every `fz_try` with other locals near it now sits in its own small function, so the build stays warning-free and a real one would show.
- **Missing letters can be the book's fault:** one EPUB's text dropped its "fi" ligatures at the source (checked in the raw file).
- **CSS warnings:** MuPDF's CSS parser reports EPUB 3 namespace selectors once per chapter. The reader silences warnings and still reports real errors through `fz_catch`.

## Tests

- **Scripted runs on the Mac**, frame by frame:
  - every screen, end to end;
  - the lock screen, sliding to unlock, the power menu, auto-sleep;
  - setting the clock;
  - USB sharing and ejecting;
  - setting a passcode, a wrong one, the right one;
  - three Library sort orders (recently read, published, author);
  - the Home grid, and covers in color.
- **0 leaks** under `leaks --atExit` (2026-11-24, a scripted session through every screen: sorting, a dictionary lookup, setting a passcode, sleep, unlocking with it, USB sharing, the power menu).
- **All 72 test books' chapters** line up with the progress weighting.
- **The ARM binary under QEMU**: frames identical to the Mac's, except at most 6 gray levels where pictures are scaled.
- **`python3 -I scripts/dict/respell.py`** checks the respelling against 33 known words.
