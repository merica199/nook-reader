/*
 * The reader's only contact with the outside world: a screen to put
 * pixels on, a stream of input events, and (where the device has
 * them) a battery, a clock, sleep and power, and a USB port. Each
 * backend fills in one mr_platform, and the reader calls through it
 * without knowing which one it got. Same idea as the kernel's struct
 * file_operations.
 *
 *   platform_fb.c      the Nook: framebuffer, evdev, sysfs
 *   platform_sdl.c     a window on the Mac
 *   platform_script.c  tests: commands on stdin, frames to files
 */
#ifndef MR_PLATFORM_H
#define MR_PLATFORM_H

#include <time.h>

/* The Nook's screen in portrait, for backends that pretend to be it. */
#define MR_NOOK_W 600
#define MR_NOOK_H 1024

/*
 * The platform reports what the finger did (down, move, up); whether
 * that was a tap, a drag or a swipe is the reader's call. x and y are
 * screen pixels.
 */
typedef enum {
	MR_EV_QUIT,
	MR_EV_NEXT,	/* forward: volume down, arrow keys */
	MR_EV_PREV,	/* back */
	MR_EV_HOME,	/* the Nook's "n" button; Esc or h on the Mac */
	MR_EV_DOWN,	/* finger down at x, y */
	MR_EV_MOVE,	/* finger moved to x, y while still down */
	MR_EV_UP,	/* finger lifted at x, y */
	MR_EV_SCROLL,	/* mouse wheel or trackpad: y pixels, + = down */
	MR_EV_TIMEOUT,	/* nothing happened before the timeout */
	MR_EV_POWER,	/* power button pressed */
	MR_EV_POWER_UP,	/* power button released */
} mr_event_type;

typedef struct {
	mr_event_type type;
	int x, y;
} mr_event;

/* What power() can do. */
enum { MR_POWER_OFF, MR_POWER_RESTART };

/* What the USB port is doing. */
enum { MR_USB_NONE, MR_USB_COMPUTER, MR_USB_SHARING };

typedef struct mr_platform {
	const char *name;

	/* Open the screen and report its size in pixels. 0 on success. */
	int (*init)(int *width, int *height);

	/*
	 * Show one full screen of 8-bit grayscale (0 black, 255 white),
	 * stride bytes per row. The backend converts it to whatever its
	 * screen really takes, so the reader never cares.
	 */
	void (*present)(const unsigned char *gray, int stride);

	/*
	 * Wait for the next event the reader cares about, at most
	 * timeout_ms milliseconds (-1: forever). If the time runs out
	 * first, the event is MR_EV_TIMEOUT. The reader uses this for
	 * press and hold, dimming, sleep and the clock.
	 */
	void (*next_event)(mr_event *ev, int timeout_ms);

	void (*shutdown)(void);

	/*
	 * Everything below is optional: NULL where the device has none,
	 * and the reader hides whatever depends on it.
	 */

	/*
	 * The backlight. Given 0-100, set it; given -1, just ask.
	 * Returns the level in percent, or -1 if it can't be read or
	 * set.
	 */
	int (*light)(int percent);

	/* Battery percent, -1 if unknown; *charging set to 0 or 1. */
	int (*battery)(int *charging);

	/*
	 * Screen off and, where the device can, the whole device asleep
	 * until a button wakes it (on = 0); awake again (on = 1). The
	 * reader redraws after waking.
	 */
	void (*sleep)(int on);

	/* Power off or restart. Returns only if it can't. */
	void (*power)(int how);

	/* Set the clock, and the hardware clock if any. 0 on success. */
	int (*set_time)(time_t t);

	/*
	 * USB: what's plugged in (MR_USB_*), and sharing the books
	 * folder with a computer as a USB drive (on = 1) or taking it
	 * back (on = 0). 0 on success. While shared, the books folder
	 * isn't there for the reader.
	 */
	int (*usb_state)(void);
	int (*usb_share)(int on);

	/*
	 * Milliseconds on a clock that only moves forward. NULL: the
	 * reader uses CLOCK_MONOTONIC. The script backend keeps its own,
	 * moved on by "." lines, so timers can be tested without
	 * waiting for them.
	 */
	long long (*now_ms)(void);
} mr_platform;

extern const mr_platform mr_platform_sdl;	/* Mac window */
extern const mr_platform mr_platform_script;	/* tests */
extern const mr_platform mr_platform_fb;	/* Nook screen */

/* The Makefile picks which one a build starts with. */
#ifndef MR_DEFAULT_PLATFORM
#define MR_DEFAULT_PLATFORM mr_platform_sdl
#endif

#endif
