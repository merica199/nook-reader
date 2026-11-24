# Nook Color reading OS: feasibility (2026-11-22)

Research done 2026-11-22, before MyNook was built (it chose the CyanogenMod kernel path; see how-it-works.md). The key claims were spot-checked against the source files listed at the end. Unverified items are marked.

## Two kernel paths

| | Modern kernel (postmarketOS `linux-postmarketos-omap`, 7.1.x) | Old CyanogenMod kernel (3.0.8) |
|---|---|---|
| Nook support | Out-of-tree patches in postmarketOS only: `omap3-nookcolor.dts` (by Scott Clark, device added 2025-05) and an LG LD070WS1 panel driver. Not in mainline Linux (checked: no nook/encore entry in mainline's OMAP dts Makefile). Device status "testing". | Board files built in; this is the kernel CM runs on the Nook today. |
| Screen | DRM/omapdss with fbdev emulation, so `/dev/fb0` exists. **Needs a custom kernel build**: the dts says `CONFIG_OMAP2_DSS_MIN_FCK_PER_PCK` must be 4, and the shared postmarketOS config has 0 (both confirmed in the saved files). | `/dev/fb0` directly, with that setting already 4. |
| Touch | Cypress TTSP driver (`=m`), fixed in a 2026-03 commit; udev calibration exists. | Built in (`=y`). |
| Buttons | gpio-keys (home, power) and the TWL keypad (volume). | Same hardware, built in. |
| Battery | MAX17042 fuel gauge and MAX8903 charger in the dts; charge-status pin may not work. Untested. | Built in; works under Android. |
| Sleep (suspend) | Code is there, but **no report that it works on this board**. Biggest unknown: it decides battery life. | Works, but through Android's early-suspend/wakelock path, so a plain system has to drive it that way. |
| USB drive mode | configfs mass storage compiled in (`=y`, confirmed). Unconfirmed on this board. | Only Android's gadget is enabled; plain `g_mass_storage` needs a rebuild. |
| Downside | Depends on one hobbyist's patches; expect churn. | 2011 kernel, no security fixes, needs an old compiler. |

## Precedents

- **okreader** (Kobo): U-Boot + kernel + Debian + KOReader booting from microSD, "very early stage". Closest design match.
- 2011-2012 Nook Color Linux attempts (Ubuntu, Arch chroot): booted, Wi-Fi worked, touch partly, nothing maintained.
- Sony PRS-T "PRSTUX": stalled.

## Ratings

| Piece | Rating |
|---|---|
| Boot to the screen | Doable (custom kernel config on the modern path) |
| Touch | Easy to doable |
| Buttons | Easy |
| Battery reading | Doable, untested |
| Suspend | **Hard**: unknown on the modern kernel |
| USB drive mode | Doable, untested |

## Build path

- **Buildroot** fits "boot straight into one app" best: kernel + the two postmarketOS patches, tiny userland, the reader started by init.
- **postmarketOS/pmbootstrap** gets to a booting system fastest, but brings more than a single-app system needs.
- MuPDF cross-compiles for ARMv7 with plain `make` (no X11), drawing to `/dev/fb0` and reading `/dev/input/event*`.
- Test from microSD only; the Nook boots the card first and internal storage stays untouched. **Needs a reliable card**: the current 8 GB one corrupts under heavy writes.

## Sources

pmaports `device/testing/device-barnesnoble-encore` and `device/community/linux-postmarketos-omap` (gitlab.postmarketos.org); LineageOS/android_kernel_bn_encore (cm-10.1-staging); verygreen/u-boot-encore; torvalds/linux `arch/arm/boot/dts/ti/omap/Makefile`; github.com/lgeek/okreader; archlinuxarm.org forum "Nook Color?"; Hackaday 2012-04-17.
