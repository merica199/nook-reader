# How it works

From power button to pixels, bottom to top.

## 1. The hardware

| Part | What | How the software sees it |
|---|---|---|
| CPU | TI OMAP3621, an ARM Cortex-A8 at about 1 GHz, NEON and hard-float VFPv3 | `-mcpu=cortex-a8`, `armhf` |
| RAM | 512 MB (459 MB usable) | |
| Screen | 7", 1024x600 LCD, mounted landscape | `/dev/fb0`, RGB565 |
| Touch | Cypress TrueTouch (`cyttsp-i2c`), multitouch | `/dev/input/event2` |
| Buttons | n and power (`gpio-keys`), volume up and down (`twl4030-keypad`, unused for now) | `/dev/input/event0`, `event1` |
| Backlight | PWM LED driver | `/sys/class/leds/lcd-backlight/` (0 to 255) |
| Battery | MAX17042 fuel gauge | `/sys/class/power_supply/max17042-0/` |
| Clock | TWL4030 real-time clock, kept running by the battery | `/dev/rtc0`, holds UTC |
| Storage | 8 GB internal eMMC, `mmcblk0`, in eight partitions | `/dev/mmcblk0p1` .. `p8` |
| USB | OTG port in device mode | Android's USB gadget, `/sys/class/android_usb/` |

The partitions that matter:

| | Holds | MyNook |
|---|---|---|
| p1 | boot: bootloader, kernels, startup archives (FAT) | the bootloader reads MyNook from here |
| p2 | `/rom`: serial number, factory data, boot counter (FAT) | never touched |
| p5 | CyanogenMod's system (ext4) | mounted read-only, for `adbd` |
| p6 | CyanogenMod's data (ext4) | MyNook's files in `/data/mynook` |
| p8 | the books, "MyNOOKcolor" (FAT) | mounted at `/storage/sdcard1`, and shared over USB |

## 2. Booting

```
boot ROM  ->  MLO  ->  u-boot.bin  ->  kernel + startup archive  ->  /init
(in chip)     (p1)      (p1)            uImage + uRamdisk  (CyanogenMod)
                                        uAltImg + uAltRam  (MyNook)
```

1. **The boot ROM**, built into the OMAP chip, loads `MLO` from the boot partition into the chip's internal RAM. `MLO` is a first-stage loader small enough to fit there. It sets up the external RAM and loads the second stage.
2. **`u-boot.bin`**, CyanogenMod's build of U-Boot, decides what to boot. Its script, read out of the binary, has three choices: `normalboot` (`uImage` + `uRamdisk`, CyanogenMod), `recoveryboot` (`uRecImg` + `uRecRam`), and `altboot` (`uAltImg` + `uAltRam`). Its screen says "Hold ^ for menu"; on the Nook that should be the **n** button (to be confirmed on the first boot). The menu's "Internal eMMC Alternate" is MyNook.
3. **The kernel** (`uAltImg`, a copy of CyanogenMod's `uImage`: Linux 3.0.8) starts with the startup archive (`uAltRam`) sitting in RAM beside it. The archive is a gzip'd cpio (a simple archive format, a list of files with their contents) behind a 64-byte U-Boot header. The kernel unpacks it into a RAM filesystem and runs `/init` from it. So MyNook lives entirely in RAM: about 20 MB, read once at boot.

Why the alternate slot and not CyanogenMod's own: every normal boot adds one to a counter on p2 (`devconf/BootCnt`), and CyanogenMod resets it once it's up. If the counter ever passes 7, the bootloader assumes the system is broken and **restores the Nook to factory settings**. `altboot` never touches the counter, so MyNook can't trip it.

The kernel's command line comes from the bootloader: `console=ttyO0,115200n8 rw init=/init ... androidboot.serialno=...`. MyNook reads the serial number from it for USB.

## 3. The kernel

MyNook uses CyanogenMod's kernel unchanged. It already has everything a reader needs built in (checked in `/proc/config.gz`): the framebuffer (`FB_OMAP2`), touch (`TOUCHSCREEN_CYTTSP`), input events (`INPUT_EVDEV`), the fuel gauge (`BATTERY_MAX17042`), the clock (`RTC_DRV_TWL4030`), FAT and ext4, and the startup archive (`BLK_DEV_INITRD`, `RD_GZIP`). It also has Android's own additions, which MyNook uses directly:

- **Wake locks and early suspend** (`HAS_WAKELOCK`, `EARLYSUSPEND`): Android's way of sleeping; see section 7.
- **Android's USB gadget** (`USB_G_ANDROID`): the USB port as a drive, adb, or both; see section 8.
- **The RAM console** (`ANDROID_RAM_CONSOLE`): the kernel's log survives a restart in RAM. If MyNook fails to boot, restart into CyanogenMod and read `/proc/last_kmsg`.

What it lacks is **devtmpfs**, the feature that fills `/dev` with device nodes by itself. MyNook's `/init` does that job with `mdev`.

## 4. MyNook

Four small files in `os/overlay/`, on top of BusyBox (one program that provides `sh`, `mount`, `init` and hundreds of other Unix commands):

- **`/init`** (run by the kernel): mounts `/proc` and `/sys`, puts a RAM filesystem on `/dev`, and runs `mdev -s`. That walks `/sys`, where the kernel lists every device it knows, and makes a node for each under the kernel's name for it: `input/event2`, `fb0`, `mmcblk0p8`, `rtc0`. Then it hands over to BusyBox `init`.
- **`/etc/inittab`** tells `init` what to run: `rcS` once, the reader forever (restarted if it ever exits), and `stop` on the way down.
- **`/etc/init.d/rcS`**:
  1. takes the wake lock `mynook`, so the Nook stays awake until the reader says otherwise;
  2. sets the system clock from the hardware clock (`hwclock -s -u`);
  3. mounts p5, p6 and p8;
  4. sets up the USB gadget;
  5. starts CyanogenMod's `adbd` from p5. It's an Android program, so it may not run outside Android, but nothing else depends on it.
- **`/usr/bin/mynook-session`** sets the environment and runs the reader. `MYNOOK=1` makes sleep, power off and USB sharing real. `TZ=EST5EDT,M3.2.0,M11.1.0` is Eastern time with its daylight saving rules, in the POSIX format musl reads. The reader's output goes to `/data/mynook/log`.

Power off and restart: the reader signals `init` (SIGUSR2 to power off, SIGTERM to restart). `init` runs `stop`, which syncs and unmounts, then asks the kernel to power off or reboot. A reboot lands in the bootloader's default, which is CyanogenMod.

## 5. The reader

### The platform layer

The reader never touches hardware directly. Everything outside it goes through one struct of function pointers, `mr_platform` in `reader/platform.h`, the same pattern as the kernel's `struct file_operations`:

```c
typedef struct mr_platform {
	int  (*init)(int *width, int *height);
	void (*present)(const unsigned char *gray, int stride);
	void (*next_event)(mr_event *ev, int timeout_ms);
	void (*shutdown)(void);
	/* optional; NULL where the device has none */
	int  (*light)(int percent);
	int  (*battery)(int *charging);
	void (*sleep)(int on);
	void (*power)(int how);
	int  (*set_time)(time_t t);
	int  (*usb_state)(void);
	int  (*usb_share)(int on);
	long long (*now_ms)(void);
} mr_platform;
```

The reader draws every frame into one 600x1024 RGB buffer (3 bytes a pixel), hands it to `present`, and asks `next_event` what happened next. The interface is drawn in grays, which in RGB are just the same byte three times; book pages, pictures and covers come out in their own colors. Three backends fill in the struct:

- **`platform_fb.c`, the Nook.** "fb" is the **framebuffer**: a block of memory the display controller scans out to the panel, exposed by the kernel as `/dev/fb0`. The backend `mmap`s it, so writing a pixel is a store to memory, like poking a memory-mapped register in C. Two conversions happen on the way out:
  - **Pixel format.** The Nook's framebuffer is RGB565 (5 bits red, 6 green, 5 blue, in one 16-bit word). Each of red, green and blue goes through its own 256-entry lookup table, built from the framebuffer's field offsets, and the three results are OR'd into one pixel.
  - **Rotation.** The panel is mounted landscape, so each frame turns a quarter turn on the way out; touches turn back on the way in.

  Input is **evdev**: each `/dev/input/eventN` is a stream of 16-byte `struct input_event` records (timestamp, type, code, value). The touchscreen speaks multitouch protocol B. Finger 0's slot, position and tracking ID arrive as separate records, and `SYN_REPORT` closes each snapshot. Only then is the position complete enough to report. The struct is spelled out by hand, because musl's `time_t` is 64-bit and the 32-bit kernel writes 32-bit timestamps. The rest is sysfs files (backlight, battery, power state, USB) and `/dev/rtc0`.
- **`platform_sdl.c`, a Mac window** at the Nook's size, through SDL2.
- **`platform_script.c`, the test harness.** Commands come in on stdin (`t 300 250` is a tap, `o` the power button, `.` time passing) and every frame goes out as a PPM image. It keeps its own clock, so timers like "sleep after 10 minutes" can be tested without waiting.

The same reader binary works under Android too (`make run`): the framebuffer, evdev and sysfs are the kernel's, not Android's. Android only has to be paused, so it lets go of the screen and the touchscreen.

### The loop

```
next_event(timeout)  ->  system.c         ->  gestures     ->  the screen showing
                         (power button,       (tap, drag,      (home.c, book.c, ...)
                          dimming, sleep,      swipe, hold)
                          lock)
```

`next_event` takes a timeout, the soonest of the reader's timers. Those are: the hold on a word, the power button becoming a hold, dimming, sleep, the lock screen going dark, the minute turning on the clock, and the next look at the USB port. The touchscreen jitters, so a finger's hold is measured from the moment it touched, not from its last tiny movement.

Each screen is a pair of functions, `draw_X` and `on_X`. After every change the whole frame is drawn again from scratch. Dark mode is a 256-entry table applied to every byte of the finished frame: paper (255) becomes 20 and ink (0) becomes 225. Book covers are drawn after that remap, so they keep their own colors; pictures inside a book's pages don't yet, and come out as negatives in dark mode.

### Books

MuPDF opens EPUBs, lays them out for the page size and font, and draws pages. Laying out a whole long book takes up to 650 ms, so books are laid out **one chapter at a time** (`fz_load_chapter_page`), and a book opens in about 5 ms. A place is saved as chapter, page, and that chapter's length in pages. If the text size changes, the page is scaled to the chapter's new length, so you land on the same passage.

Progress is weighted by the size of each chapter's file inside the EPUB, read straight from its zip (`epub.c`). So 50% means halfway through the text.

The state file (`/data/mynook/state`) is plain text, written on every page turn the safe way: to a temporary file, `fsync`, then `rename`. A dead battery can't leave half a file.

- **Settings:** lines like `#dark<TAB>1`.
- **Books:** one line per book, tab-separated: path, size and modification time (to notice a changed file), the saved place, when it was last read, title, author, percent, and the published date from the EPUB's `dc:date`. Calibre writes year 0101 for "unknown", which counts as no date.

The Library sorts by any of five orders. Home, the lock screen and start-up don't depend on that order: they ask for the most recently read books separately (`mr_library_recent`).

**The passcode** is kept as a 32-bit FNV-1a hash of the digits, never the digits themselves. That keeps it out of plain sight in the state file. It isn't security against someone with adb: they can delete the line.

### The dictionary

Built once on a computer from Wiktionary (`scripts/dict/build_dict.py`) into two files:

- **`dict.dat`**: one record per word, `key NUL see NUL html`. The key is the word with A-Z lowered. `see` is a base word to show as well ("run" for "ran"). `html` is the entry, ready to lay out.
- **`dict.idx`**: an 8-byte magic, a count, then (offset, length) pairs into `dict.dat`, sorted by key bytes.

The reader `mmap`s both and binary-searches with `strcmp`, so nothing loads at start-up: a lookup is about 20 steps of binary search over a million words, each reading one page of the index and one of the entries. Holding a word: MuPDF's structured text gives every character on the page with its box. The reader finds the nearest character in the line under the finger and widens it to the word. The entry is laid out by MuPDF as HTML, so it gets proper wrapping, italics and accented letters, and scrolls if it's long.

The respellings ("ig-ZAM-puhl") are made from Wiktionary's IPA by `scripts/dict/respell.py`:

1. Split the IPA into sounds.
2. Split those into syllables: one vowel each, with the longest consonant cluster that can start an English syllable going to the next one.
3. Let a short vowel keep the next consonant ("HAP-ee", not "HA-pee").
4. Spell each sound plainly and capitalize the stressed syllable.

American pronunciations are preferred.

## 6. Building

Everything for the Nook is built with **GCC 15 and musl**, made by Buildroot inside a Docker container (`os/Dockerfile`), because Buildroot needs Linux and a case-sensitive disk. The reader is linked **statically**: MuPDF, musl and the C++ runtime (for HarfBuzz, MuPDF's text shaper) are all inside one 16.7 MB binary. That's why one file runs both under Android (whose C library is bionic) and under MyNook.

`make os` runs Buildroot with `os/mynook_defconfig`: BusyBox, `os/overlay`, and the reader, packed as `rootfs.cpio.uboot`, which is `uAltRam`.

## 7. Sleep

Android's kernel sleeps in two steps:

1. Writing `mem` to `/sys/power/state` runs the "early suspend" handlers right away (display and backlight off).
2. The device really suspends as soon as no **wake lock** is held.

MyNook takes the wake lock `mynook` at boot. To sleep, the reader writes `mem` and then releases `mynook`. The power button is wired as a wake-up source: pressing it resumes the kernel, and evdev holds its own wake lock until the reader has read that press. The reader then takes `mynook` back and writes `on`, the display comes back, and the lock screen is drawn.

Plugged into a computer, the Nook doesn't sleep by itself: suspended, the USB link drops, and the computer would see its drive vanish. A plain charger doesn't count.

## 8. USB

Android's gadget driver makes the USB port look like a device of your choosing. MyNook sets it up once, the way CyanogenMod's `init.encore.usb.rc` does: vendor 2080, product 0002 (Barnes & Noble's), functions `mass_storage,adb`. The drive starts empty, like a card reader with no card in it.

**Sharing the books** unmounts p8 and writes `/dev/mmcblk0p8` into the drive's backing file (`f_mass_storage/lun0/file`). That's the card going in. Ejecting on the computer empties that file. The reader notices on its next look, mounts p8 again and rescans the library. Nothing re-enumerates, so adb stays connected throughout. The two systems never have the partition mounted at once, which is what keeps a FAT filesystem safe.

## 9. Testing

1. **On the Mac**: the window for trying things by hand; the script harness for repeatable runs that produce images to compare.
2. **The ARM binary, under QEMU**: `docker run --platform linux/arm/v7` runs the Nook build itself on the Mac, through the script harness. On the same walk, its frames match the Mac's byte for byte, except where a picture is scaled (at most 6 gray levels apart, from floating point on 32-bit ARM).
3. **On the Nook under Android** (`make run`): the real screen, touch and buttons.
4. **On the Nook as MyNook**: everything else (booting, sleep, USB, power).
