# nook-reader

**MyNook** and its reader are by [@merica199](https://github.com/merica199).

An e-reader for the Barnes & Noble Nook Color (2010), and **MyNook**, the tiny Linux system it runs on. The Nook turns on into the book you were reading. The n button goes Home; Home, Library and Settings are one tap apart. There's no launcher, no store and no separate app to open: just the reader, the way the Nook's own software worked, without the clutter.

The reader is about 6,000 lines of C on [MuPDF](https://mupdf.com/). MyNook is BusyBox and four short shell scripts, booted by the Nook's existing CyanogenMod kernel and bootloader. It installs **beside** CyanogenMod: two new files on the boot partition, chosen from the boot menu, or started at every power-on if you make it the default. Nothing that's already there is replaced.

## Which Nook this is for

> **Only the Barnes & Noble Nook Color** (model BNRV200, codename `encore`, released 2010; 7" color LCD, 1024x600), running **CyanogenMod 10.2** with root over adb. It was built and tested on one such Nook, running CyanogenMod 10.2.1.

It will **not** work on other Nooks, even though several look similar:

| Device | Why not |
|---|---|
| Nook Tablet (BNTV250, `acclaim`) | different board, kernel and bootloader |
| Nook Simple Touch / GlowLight (BNRV300, BNRV350, later) | E Ink screen, different hardware |
| Nook HD and HD+ (`hummingbird`, `ovation`) | different hardware |
| The original Nook (BNRV100) | E Ink, different hardware |
| Samsung Galaxy Tab "Nook" editions | Samsung tablets, not Nooks |
| A Nook Color on the stock B&N software, or another ROM | MyNook needs CyanogenMod's kernel and its bootloader's alternate boot slot |

The installer checks before writing anything: it refuses unless the device reports itself as `encore`, adb runs as root, and the bootloader has the alternate slot. The reader also builds and runs on a Mac (below), which needs no Nook at all.

## What it does

- **Home:** the book you're reading, one tap to carry on, and a grid of the eight books read before it.
- **Library:** every book in one scrolling list, sorted by last read, title, author, date added or date published.
- **Reading:** tap the right two-thirds of the page for the next page, the left third to go back, or swipe. Tap the top or bottom edge for the reading menu: progress, Home, Contents, Settings. Pages, covers and pictures are in color on the Nook Color's screen.
- **Dictionary:** press and hold a word for half a second. Its entry slides up over the other half of the screen: the word, how to say it ("ig-ZAM-puhl"), and numbered meanings. Inflected words show their base word too ("went", then "go"). It covers 1,042,340 English words from Wiktionary and works offline.
- **Contents:** the book's table of contents, with the current chapter marked.
- **Settings:**
  - Reading: text size (7 to 28 pt), font (the book's own, serif or sans), line spacing and margins, with a live preview. These are the defaults; any book can have its own instead ("For: This book"), and keeps them.
  - Device: dark mode (on by default), brightness, sleep after (30 seconds to 5 minutes; the screen dims 10 seconds before), 12 or 24 hour clock, the date and time, and the passcode.
- **Lock screen:** a big clock, the date and the battery, and nothing about what you're reading; slide the knob across to unlock, with an optional 4 to 6 digit passcode. The power button sleeps and wakes; hold it for Power off or Restart.
- **USB drive:** plug into a computer and choose "Share books". The books appear as a drive; eject it when you're done and reading picks up again.
- **Progress** is weighted by chapter size, so 50% means halfway through the text, not halfway through the chapters. Your place is saved on every page turn.

## Trying it on a Mac

The same code runs in a window, at the Nook's screen size:

```
brew install mupdf sdl2
make reader
reader/reader ~/path/to/epub/folder
```

Click to tap, hold the mouse button still for half a second to look up a word, drag to scroll, and use the arrow keys to turn pages. Esc is the n button and `l` is the power button (hold it for the power menu).

## Putting it on a Nook

You need a Nook Color running CyanogenMod 10.2 with root over adb. Everything here was built and tested on one such Nook. With the Nook plugged in and CyanogenMod running:

```
make install
```

This backs up the boot partition to `os/out/p1-backup.img`, then adds `uAltImg` and `uAltRam` beside CyanogenMod's files. It also copies the dictionary to `/data/mynook`.

To start MyNook, power off. Then power on holding the **n** button until the boot menu appears (the bootloader's screen says "Hold ^ for menu"), and choose **Internal eMMC Alternate**. Starting normally still boots CyanogenMod.

To have the Nook start MyNook at every power-on, with nothing to hold:

```
scripts/nook-install.sh default     # and "nodefault" to undo
```

This sets the bootloader's own switch: a one-byte file, `u-boot.altboot`, on the `/rom` partition. It's the only thing MyNook ever writes there (`/rom` holds the Nook's serial number and factory data). With it set, CyanogenMod is in the boot menu (hold n, choose **Internal eMMC Normal**), and MyNook's **Restart** comes back to MyNook.

**Updating** after that needs neither CyanogenMod nor adb. Under MyNook, plug into the computer and tap **Share books**, then:

```
make update
```

That builds MyNook and copies it, with its SHA-256, into `MyNook-update/` on the Nook's drive, then ejects it. MyNook takes the drive back, checks the file, installs it, and restarts into it. The version it replaced stays on the boot partition as `uAltRam.old`. The running version is shown at the bottom of Settings, Device. Versions are MAJOR.MINOR.PATCH, from the `VERSION` file: patch releases fix bugs, minor releases add features, and 1.0.0 comes when everything listed as unverified in `docs/notes.md` has been checked on a Nook.

To uninstall: `scripts/nook-install.sh remove`, which deletes the two files.

MyNook expects the books in `My Files/Books` on the internal books partition (the "MyNOOKcolor" drive). It keeps its own files in `/data/mynook`: saved places, settings, the dictionary, and a log.

## Building from nothing

The reader for the Nook is built with GCC 15 and musl, which Buildroot builds inside Docker (Buildroot doesn't run on macOS). You need Docker, Python 3, `adb`, and Homebrew's `mupdf` and `sdl2` for the Mac build.

```
make fetch       # MuPDF 1.27.2 and Buildroot 2026.08.2, checked by SHA-256
make toolchain   # GCC and musl for the Cortex-A8 (about 11 minutes)
make mupdf       # MuPDF for the Nook (about a minute)
make os          # the reader for the Nook, then MyNook: os/out/uAltRam
make dict        # the dictionary: a 3.4 GB download, then 31 seconds
make install     # onto the Nook, over adb
```

`make run` tries a new reader build on the Nook without restarting. It pauses Android, runs the reader, and brings Android back when you quit.

## Documentation

- [docs/user-guide.md](docs/user-guide.md): using it, from starting MyNook to adding books and recovering a forgotten passcode.
- [docs/how-it-works.md](docs/how-it-works.md) walks the whole stack, from the boot ROM to a pixel on the screen. It covers the bootloader's alternate slot, the kernel and its startup archive, init, the platform layer (how one program drives a Mac window and the Nook's framebuffer), MuPDF, the dictionary's file format, sleep, and USB.
- [docs/notes.md](docs/notes.md): what's verified on the device and what isn't yet, and lessons learned.
- [docs/early-research.md](docs/early-research.md): the research done before MyNook was built.

## Layout

| Path | What |
|---|---|
| `reader/` | the reader (C). `platform_fb.c` is the Nook, `platform_sdl.c` the Mac window, `platform_script.c` the test harness |
| `os/` | MyNook: the Buildroot config, the Docker build box, and `overlay/`, MyNook's own files (`/init`, `inittab`, startup, session) |
| `scripts/` | fetching sources, building the toolchain and MyNook, installing, running under Android; `dict/` builds the dictionary |
| `docs/` | the user guide, how it works, development notes, and the early research |

## Status

The reader runs on the Nook under Android (tested 2026-11-23 and 24): screen, touch, buttons, backlight and the dictionary all work. MyNook's first boot is the step in progress. See [docs/notes.md](docs/notes.md) for what's verified and what isn't yet.

## Credits and licenses

**Author:** [@merica199](https://github.com/merica199), creator of MyNook and the reader.

nook-reader is licensed under the [GNU AGPL v3](LICENSE), the license of MuPDF, which it's built on.

- [MuPDF](https://mupdf.com/) by Artifex: AGPL v3.
- [Wiktionary](https://en.wiktionary.org/), via [kaikki.org](https://kaikki.org/)'s extract, is the dictionary's source: CC BY-SA. The dictionary is built from it, not stored in this repository.
- [Buildroot](https://buildroot.org/) and [BusyBox](https://busybox.net/): GPL v2.
- MyNook boots the [CyanogenMod](https://github.com/LineageOS/android_kernel_bn_encore) kernel and bootloader already on the Nook: GPL v2.
