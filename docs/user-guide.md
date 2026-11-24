# User guide

How to use the reader on a Nook Color running MyNook. (For how it's built, see [how-it-works.md](how-it-works.md).)

> **For the Barnes & Noble Nook Color (BNRV200) running CyanogenMod 10.2 only.** No other Nook, including the Nook Tablet, Simple Touch, GlowLight or HD; see [the README](../README.md#which-nook-this-is-for).

## Starting and stopping

- **Start MyNook:** press power. If MyNook has been made the default (see the README), that's all. If not, hold the **n** button while pressing power, keep holding it until the boot menu appears, and choose **Internal eMMC Alternate**.
- **Start CyanogenMod instead:** hold n while pressing power, and choose **Internal eMMC Normal**.
- **Sleep:** press power. The screen goes dark and the Nook sleeps. It also sleeps by itself after a while with nothing touched (Settings, Device, Sleep after).
- **Wake:** press power (or n). The lock screen appears.
- **Power off or restart:** hold power for a second and a half. Choose **Power off** or **Restart**. Restart starts whichever system the Nook starts by default.

## The lock screen

The lock screen shows the time, the date and the battery. It doesn't show what you're reading. Drag the **>** knob all the way to the right to unlock. Tapping doesn't unlock, so it can't open in a bag. If you leave the lock screen alone for 30 seconds, it goes dark again.

**Passcode:** Settings, Device, Passcode, **Set**. Choose 4 to 6 digits and type them twice. From then on the keypad asks for it after the slider. **Change** and **Turn off** ask for the current passcode first.

The passcode keeps casual hands out; it isn't encryption. If you forget it: start CyanogenMod, connect the Nook to a computer with adb, and remove the passcode line from the saved settings:

```
adb shell "sed -i '/^#pass/d' /data/mynook/state"
```

## Home

The n button always comes here. At the top is the book you're reading: tap it to carry on where you left off. Below are the books you read before it, most recent first; tap one to open it. The tabs along the bottom go to the Library and Settings.

## Library

Every book on the Nook, in one list. Drag to scroll; tap a book to open it. Tapping the Library tab again jumps back to the top.

**Sort** (top right) picks the order:
- **Recently read:** the book you're reading first, then the ones before it, then the rest by title.
- **Title:** A to Z, ignoring "The", "A" and "An".
- **Author:** by last name.
- **Date added:** newest on the Nook first, with the date shown under each book.
- **Date published:** newest first, with the year shown. Books whose files don't say go at the end.

## Reading

- **Next page:** tap the right two-thirds of the page, or swipe left.
- **Previous page:** tap the left third, or swipe right.
- **The reading menu:** tap the top or bottom edge. It shows how far along you are, with buttons for Home, Contents and Settings. Any page turn puts it away.
- **Contents:** the book's chapters, with the one you're in marked. Tap one to go there.
- Along the bottom of every page: the title, the time, and how far through the book you are.

Your place is saved on every page turn, so turning the Nook off never loses it.

### Looking up a word

Put your finger on a word and keep it there for half a second. The word is highlighted and its definition slides up over the other half of the screen. You get:

- the word in bold;
- how to say it, in plain letters with the stressed part in capitals ("ig-ZAM-puhl");
- numbered meanings for each part of speech.

For a word like "went", you also get the base word ("go") underneath. Drag the definition to scroll a long one. Tap the page to put it away, or hold another word to look that up instead.

The dictionary is Wiktionary's English, about a million words, stored on the Nook, so it works without Wi-Fi.

## Settings

**Reading** (the paragraph at the bottom shows the result as you change things). These are your defaults, which every book follows. With a book open, a **For** row appears at the top. Choose **This book** to give that book its own settings: changes from then on apply to it alone, and it keeps them. Choosing **All books** again puts it back on the defaults.
- **Text size:** 7 to 28 points.
- **Font:** the book's own, or always serif, or always sans-serif.
- **Line spacing** and **Margins.**

**Device:**
- **Dark mode:** light text on a dark page, easier at night. Covers keep their own colors.
- **Brightness.**
- **Sleep after:** 30 seconds to 5 minutes untouched, and the Nook sleeps. Tap the row to pick. Ten seconds before, the screen dims as a warning; any touch brightens it again, without turning the page, and starts the time over. On a charger, or plugged into a computer with **Charge only**, it dims and sleeps as usual. While it's connected to a computer as a drive, it dims but doesn't sleep, so a copy is never cut off; eject it to let it sleep.
- **Clock:** 12 hour ("2:05 PM") or 24 hour ("14:05").
- **Date & time:** set the clock. The Nook has no network to set it, so it drifts a little over weeks.
- **Passcode:** see above.

**Done** (top right, while a book is open) goes back to the page.

## Adding and removing books

Plug the Nook into a computer. It asks what you want: choose **Connect to computer** and the Nook's storage appears on the computer as a drive, or **Charge only** to just charge. (A wall charger never asks.) Put EPUB files anywhere under `My Files/Books`; Calibre's "Send to device" or plain copying both work. When you're done, **eject the drive on the computer**. The Nook takes its books back, finds the new ones, and shows how many it has.

Reading is paused while the books are shared, and the power button waits until the drive is ejected.

## Updating MyNook

A new version comes the same way as books. Connect to the computer, put the `MyNook-update` folder you were given (or that `make update` copies) at the top of the drive, and eject. The Nook shows "Updating MyNook" for a few seconds and restarts into the new version. Don't turn it off while it says that. If the file was damaged in copying, it says so and changes nothing; copy it again.

Which version is running is at the bottom of Settings, Device.

## Battery

The battery is at the top of every screen, an icon and a percentage, with a lightning bolt while charging. A computer's USB port charges slowly: it gives about a quarter of what the Nook's own charger does, and an awake Nook uses most of that itself. Use the wall charger to charge quickly. The lock screen shows it large, with "Charging". Plugging in or unplugging lights up the lock screen for a moment, so you can see it took. The lock screen warns when it gets low. Asleep, the Nook should use very little power; how long a charge lasts under MyNook hasn't been measured yet.

## When something goes wrong

- **MyNook doesn't start, or starts to a blank screen:** hold power to turn it off, start CyanogenMod normally, and connect with adb. Two places to look:
  - MyNook's log: `/data/mynook/log`;
  - the kernel's log from the failed start: `/proc/last_kmsg`.
- **Uninstall MyNook:** from CyanogenMod, run `scripts/nook-install.sh remove` on the computer. Your saved places and the dictionary stay in `/data/mynook`.
