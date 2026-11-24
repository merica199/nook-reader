/*
 * Backend: the Nook itself, through plain Linux interfaces, so it
 * works the same under MyNook and under CyanogenMod with Android
 * paused (scripts/nook-run.sh):
 *
 *   screen    the framebuffer: /dev/graphics/fb0 (Android) or /dev/fb0
 *   input     evdev: /dev/input/event0 .. event15
 *   light     /sys/class/leds/lcd-backlight
 *   battery   /sys/class/power_supply/max17042-0 (the fuel gauge)
 *   sleep     /sys/power/state and wake_lock (Android's kernel)
 *   clock     settimeofday, and /dev/rtc0 for the hardware clock
 *   USB       /sys/class/android_usb/android0 (Android's USB gadget)
 *
 * The panel is landscape (1024x600) and the reader is portrait
 * (600x1024), so frames turn a quarter turn on the way out and touches
 * turn back on the way in. If the picture comes out upside down, run
 * with MR_ROTATE=ccw to turn the other way.
 *
 * Buttons: n (KEY_HOME) is home; power is reported pressed and
 * released, so the reader can tell a press (sleep) from a hold (the
 * power-off menu). The volume buttons do nothing.
 *
 * MyNook's init sets MYNOOK=1. Without it (testing under Android),
 * sleep only turns the backlight off, "power off" just returns (the
 * reader then quits and the script restarts Android), and USB is
 * left to Android's own setup.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/rtc.h>
#include "platform.h"

static int native;	/* running as MyNook, not under Android */

/* ---- screen ---- */

static int fb_fd = -1;
static uint8_t *fb_mem;
static size_t fb_len;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;

static int rotated;	/* panel is landscape, reader is portrait */
static int ccw;		/* turn the other way (MR_ROTATE=ccw) */
static int pw, ph;	/* the size the reader sees */

/*
 * One byte of red, green or blue to its bits in a screen pixel,
 * whatever the screen's format: three tables, OR'd together.
 */
static uint32_t lut_r[256], lut_g[256], lut_b[256];

static uint32_t pack(unsigned v, const struct fb_bitfield *f)
{
	return f->length ? (v >> (8 - f->length)) << f->offset : 0;
}

static int open_screen(void)
{
	const char *paths[] = { "/dev/graphics/fb0", "/dev/fb0" };
	for (size_t i = 0; i < 2 && fb_fd < 0; i++)
		fb_fd = open(paths[i], O_RDWR);
	if (fb_fd < 0) {
		perror("framebuffer");
		return -1;
	}
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) != 0 ||
	    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) != 0) {
		perror("framebuffer info");
		return -1;
	}
	if (var.bits_per_pixel != 16 && var.bits_per_pixel != 32) {
		fprintf(stderr, "framebuffer: %u bits per pixel, "
			"only 16 and 32 handled\n", var.bits_per_pixel);
		return -1;
	}

	fb_len = fix.smem_len;
	fb_mem = mmap(NULL, fb_len, PROT_READ | PROT_WRITE, MAP_SHARED,
		      fb_fd, 0);
	if (fb_mem == MAP_FAILED) {
		perror("framebuffer mmap");
		fb_mem = NULL;
		return -1;
	}

	/*
	 * RGB565 on the Nook: 5 bits red, 6 green, 5 blue. Each channel
	 * keeps its top bits. Opaque alpha (in the red table, so it's
	 * added once) if there is an alpha channel at all.
	 */
	for (unsigned v = 0; v < 256; v++) {
		lut_r[v] = pack(v, &var.red) | pack(255, &var.transp);
		lut_g[v] = pack(v, &var.green);
		lut_b[v] = pack(v, &var.blue);
	}

	rotated = var.xres > var.yres;
	pw = rotated ? var.yres : var.xres;
	ph = rotated ? var.xres : var.yres;
	const char *r = getenv("MR_ROTATE");
	ccw = r && strcmp(r, "ccw") == 0;

	fprintf(stderr, "screen: %ux%u, %u bpp, line %u bytes%s\n",
		var.xres, var.yres, var.bits_per_pixel, fix.line_length,
		rotated ? (ccw ? ", turned ccw" : ", turned cw") : "");
	return 0;
}

/*
 * Which portrait pixel (px, py) shows at panel pixel (lx, ly). Used
 * both ways: drawing walks the panel and asks which page pixel goes
 * there; a touch lands on the panel and asks which page pixel it hit.
 *
 * cw:  panel x runs down the page, panel y runs right to left
 *      (the top of the page is along the panel's left edge)
 * ccw: the opposite quarter turn
 */
static void from_panel(int lx, int ly, int *px, int *py)
{
	if (!rotated) {
		*px = lx;
		*py = ly;
	} else if (!ccw) {
		*px = pw - 1 - ly;
		*py = lx;
	} else {
		*px = ly;
		*py = ph - 1 - lx;
	}
}

static void fb_present(const unsigned char *rgb, int stride)
{
	int bpp = var.bits_per_pixel / 8;
	uint8_t *base = fb_mem + var.yoffset * fix.line_length +
			var.xoffset * bpp;

	/*
	 * Walk the panel row by row so writes to the framebuffer are
	 * sequential (it's uncached memory on many boards), and pick
	 * the matching portrait pixel for each.
	 */
	for (int ly = 0; ly < (int)var.yres; ly++) {
		uint8_t *row = base + ly * fix.line_length;
		for (int lx = 0; lx < (int)var.xres; lx++) {
			int px, py;
			from_panel(lx, ly, &px, &py);
			const unsigned char *p = rgb + py * stride + px * 3;
			uint32_t v = lut_r[p[0]] | lut_g[p[1]] | lut_b[p[2]];
			if (bpp == 2)
				((uint16_t *)row)[lx] = (uint16_t)v;
			else
				((uint32_t *)row)[lx] = v;
		}
	}
	/* some drivers only refresh on a pan; harmless elsewhere */
	ioctl(fb_fd, FBIOPAN_DISPLAY, &var);
}

/* ---- input ---- */

#define MAX_INPUTS 16

/*
 * The kernel's struct input_event, spelled out: two longs of
 * timestamp, then type, code, value. Using the C library's own struct
 * isn't safe here, because a 64-bit time_t (as in newer musl) would
 * make it bigger than what a 32-bit kernel writes.
 */
struct raw_event {
	unsigned long sec, usec;
	uint16_t type, code;
	int32_t value;
};

struct input {
	int fd;
	int max_x, max_y;	/* touch ranges, to scale to the panel */
	int slot;		/* multitouch: which finger is reporting */
	int touching;
	int x, y;		/* raw position of finger 0 */
	int want_down, want_up, moved;
};

static struct input inputs[MAX_INPUTS];
static struct pollfd polls[MAX_INPUTS];
static int n_inputs;

static int abs_max(int fd, int code)
{
	struct input_absinfo info;
	if (ioctl(fd, EVIOCGABS(code), &info) != 0 || info.maximum <= 0)
		return 0;
	return info.maximum;
}

static void open_inputs(void)
{
	for (int i = 0; i < MAX_INPUTS; i++) {
		char path[32];
		snprintf(path, sizeof path, "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		struct input *in = &inputs[n_inputs];
		memset(in, 0, sizeof *in);
		in->fd = fd;
		/* the Nook's touchscreen is multitouch only */
		in->max_x = abs_max(fd, ABS_MT_POSITION_X);
		in->max_y = abs_max(fd, ABS_MT_POSITION_Y);
		if (!in->max_x)
			in->max_x = abs_max(fd, ABS_X);
		if (!in->max_y)
			in->max_y = abs_max(fd, ABS_Y);
		polls[n_inputs].fd = fd;
		polls[n_inputs].events = POLLIN;
		n_inputs++;
	}
	fprintf(stderr, "input: %d devices\n", n_inputs);
}

/* A small queue between the kernel's events and the reader's. */
#define QLEN 32
static mr_event queue[QLEN];
static int q_head, q_count;

static void push(mr_event_type type, int x, int y)
{
	mr_event e = { type, x, y };
	/* a newer move replaces an unread one: never fall behind */
	if (type == MR_EV_MOVE && q_count > 0) {
		mr_event *last = &queue[(q_head + q_count - 1) % QLEN];
		if (last->type == MR_EV_MOVE) {
			*last = e;
			return;
		}
	}
	if (q_count == QLEN)
		return;
	queue[(q_head + q_count++) % QLEN] = e;
}

/* Raw touch position to portrait screen pixels. */
static void touch_point(struct input *in, int *px, int *py)
{
	int lx = in->max_x ? in->x * ((int)var.xres - 1) / in->max_x
			   : in->x;
	int ly = in->max_y ? in->y * ((int)var.yres - 1) / in->max_y
			   : in->y;
	from_panel(lx, ly, px, py);
}

static void handle(struct input *in, const struct raw_event *ev)
{
	switch (ev->type) {
	case EV_KEY:
		if (ev->code == KEY_POWER) {
			if (ev->value == 1)
				push(MR_EV_POWER, 0, 0);
			else if (ev->value == 0)
				push(MR_EV_POWER_UP, 0, 0);
			break;
		}
		if (ev->value != 1)	/* presses only, not repeats */
			break;
		switch (ev->code) {
		case KEY_HOME:
			push(MR_EV_HOME, 0, 0);
			break;
		/*
		 * Volume up and down do nothing yet: nothing makes a
		 * sound. (KEY_VOLUMEDOWN and KEY_VOLUMEUP, from the
		 * twl4030 keypad, are where to start if they ever should.)
		 */
		case BTN_TOUCH:
		case BTN_LEFT:
			in->want_down = 1;
			break;
		}
		if ((ev->code == BTN_TOUCH || ev->code == BTN_LEFT) &&
		    ev->value == 0)
			in->want_up = 1;
		break;

	case EV_ABS:
		switch (ev->code) {
		case ABS_MT_SLOT:
			in->slot = ev->value;
			break;
		case ABS_MT_TRACKING_ID:
			/* only the first finger counts */
			if (in->slot != 0)
				break;
			if (ev->value >= 0)
				in->want_down = 1;
			else
				in->want_up = 1;
			break;
		case ABS_MT_POSITION_X:
		case ABS_X:
			if (in->slot == 0) {
				in->x = ev->value;
				in->moved = 1;
			}
			break;
		case ABS_MT_POSITION_Y:
		case ABS_Y:
			if (in->slot == 0) {
				in->y = ev->value;
				in->moved = 1;
			}
			break;
		}
		break;

	case EV_SYN:
		/*
		 * SYN_REPORT closes one snapshot of the finger: only now
		 * are x and y both current, so only now report.
		 */
		if (ev->code != SYN_REPORT)
			break;
		int px, py;
		touch_point(in, &px, &py);
		if (in->want_down && !in->touching) {
			push(MR_EV_DOWN, px, py);
			in->touching = 1;
		} else if (in->touching && in->moved) {
			push(MR_EV_MOVE, px, py);
		}
		if (in->want_up && in->touching) {
			push(MR_EV_UP, px, py);
			in->touching = 0;
		}
		in->want_down = in->want_up = in->moved = 0;
		break;
	}
}

static void fb_next_event(mr_event *ev, int timeout_ms)
{
	while (q_count == 0) {
		int r = n_inputs ? poll(polls, n_inputs, timeout_ms) : -1;
		if (r < 0) {
			ev->type = MR_EV_QUIT;
			return;
		}
		/*
		 * Timed out. (Input that queues nothing, like a finger
		 * resting still, can stretch the wait a little past
		 * timeout_ms; the reader keeps its own deadline.)
		 */
		if (r == 0) {
			ev->type = MR_EV_TIMEOUT;
			return;
		}
		for (int i = 0; i < n_inputs; i++) {
			if (!(polls[i].revents & POLLIN))
				continue;
			struct raw_event buf[64];
			ssize_t n = read(inputs[i].fd, buf, sizeof buf);
			for (ssize_t k = 0; k < n / (ssize_t)sizeof *buf;
			     k++)
				handle(&inputs[i], &buf[k]);
		}
	}
	*ev = queue[q_head];
	q_head = (q_head + 1) % QLEN;
	q_count--;
}

/* ---- sysfs files ---- */

static int read_int(const char *path)
{
	FILE *f = fopen(path, "r");
	int v = -1;
	if (f) {
		if (fscanf(f, "%d", &v) != 1)
			v = -1;
		fclose(f);
	}
	return v;
}

/* The file's first line, without its newline. "" if unreadable. */
static void read_line(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	buf[0] = '\0';
	if (f) {
		if (!fgets(buf, (int)n, f))
			buf[0] = '\0';
		fclose(f);
	}
	buf[strcspn(buf, "\n")] = '\0';
}

static int write_str(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	ssize_t n = write(fd, s, strlen(s));
	close(fd);
	return n == (ssize_t)strlen(s) ? 0 : -1;
}

/* ---- backlight ---- */

/* The LED-class backlight (checked on the Nook 2026-11-24: max 255). */
#define LIGHT_DIR "/sys/class/leds/lcd-backlight/"

static int light_raw(int level)
{
	char buf[16];
	snprintf(buf, sizeof buf, "%d\n", level);
	return write_str(LIGHT_DIR "brightness", buf);
}

static int fb_light(int percent)
{
	int max = read_int(LIGHT_DIR "max_brightness");
	if (max <= 0)
		return -1;
	if (percent >= 0) {
		/* never fully dark: 5% keeps the screen findable */
		if (percent < 5)
			percent = 5;
		if (percent > 100)
			percent = 100;
		if (light_raw(max * percent / 100) != 0)
			return -1;
	}
	int now = read_int(LIGHT_DIR "brightness");
	return now < 0 ? -1 : (now * 100 + max / 2) / max;
}

/* ---- battery ---- */

/* The MAX17042 fuel gauge (checked on the Nook 2026-11-24). */
#define BATTERY "/sys/class/power_supply/max17042-0/"

static int fb_battery(int *charging)
{
	char status[32];
	read_line(BATTERY "status", status, sizeof status);
	*charging = strcmp(status, "Charging") == 0 ||
		    strcmp(status, "Full") == 0;
	return read_int(BATTERY "capacity");
}

/* ---- sleep ---- */

/*
 * Android's kernel sleeps in two steps. Writing "mem" to
 * /sys/power/state runs the "early suspend" handlers (display and
 * backlight off) at once; the device then really suspends as soon as
 * no wake lock is held. MyNook's init takes the wake lock "mynook" at
 * boot, so letting go of it is what lets the device sleep. A button
 * wakes it; evdev holds its own wake lock until the reader has read
 * that press, and the reader takes "mynook" back before saying "on".
 */
static void fb_sleep(int on)
{
	if (!on) {
		light_raw(0);
		if (native) {
			write_str("/sys/power/state", "mem");
			write_str("/sys/power/wake_unlock", "mynook");
		}
	} else if (native) {
		write_str("/sys/power/wake_lock", "mynook");
		write_str("/sys/power/state", "on");
	}
}

/* ---- power ---- */

/*
 * BusyBox init is process 1 under MyNook: SIGUSR2 asks it to power
 * off, SIGTERM to reboot, after unmounting everything (os/inittab).
 * A reboot lands in CyanogenMod, the bootloader's default.
 */
static void fb_power(int how)
{
	if (!native)
		return;
	sync();
	kill(1, how == MR_POWER_RESTART ? SIGTERM : SIGUSR2);
	for (;;)
		pause();	/* init stops us on its way down */
}

/* ---- clock ---- */

/*
 * The system clock, then the hardware clock (the TWL4030's RTC, kept
 * running by the battery). The RTC holds UTC, as Android left it;
 * the time zone is only for showing the time (TZ, from init).
 */
static int fb_set_time(time_t t)
{
	struct timeval tv = { .tv_sec = t };
	if (settimeofday(&tv, NULL) != 0)
		return -1;

	struct tm tm;
	gmtime_r(&t, &tm);
	struct rtc_time rt = {
		.tm_sec = tm.tm_sec, .tm_min = tm.tm_min,
		.tm_hour = tm.tm_hour, .tm_mday = tm.tm_mday,
		.tm_mon = tm.tm_mon, .tm_year = tm.tm_year,
	};
	int fd = open("/dev/rtc0", O_WRONLY);
	if (fd < 0)
		fd = open("/dev/rtc", O_WRONLY);
	if (fd < 0)
		return 0;	/* the system clock is set; RTC unknown */
	int r = ioctl(fd, RTC_SET_TIME, &rt);
	close(fd);
	return r == 0 ? 0 : -1;
}

/* ---- USB ---- */

/*
 * Android's USB gadget. MyNook's start-up (os/overlay/etc/init.d/rcS)
 * sets it up once as "mass_storage,adb", the way CyanogenMod's own
 * init.encore.usb.rc does, with the drive (lun0) empty: to a computer
 * it's a card reader with no card in it. Sharing the books is putting
 * the books partition in the slot; ejecting on the computer takes it
 * out again. Nothing re-enumerates, so adb stays connected throughout.
 */
#define USB "/sys/class/android_usb/android0/"
#define LUN USB "f_mass_storage/lun0/file"

static int sharing;

static int fb_usb_state(void)
{
	char s[32];
	if (!native)
		return MR_USB_NONE;
	read_line(USB "state", s, sizeof s);
	/* CONFIGURED: a computer, not just a charger, set the port up */
	if (strcmp(s, "CONFIGURED") != 0)
		return MR_USB_NONE;
	if (sharing) {
		/* ejecting on the computer empties the drive */
		read_line(LUN, s, sizeof s);
		if (s[0])
			return MR_USB_SHARING;
	}
	return MR_USB_COMPUTER;
}

static int fb_usb_share(int on)
{
	const char *dev = getenv("MYNOOK_BOOKS_DEV");
	const char *dir = getenv("MYNOOK_BOOKS_DIR");
	if (!native || !dev || !dir)
		return -1;
	if (on) {
		/* the books can't be mounted here and there at once */
		sync();
		if (umount(dir) != 0 && errno != EINVAL) {
			perror("unmount books");
			return -1;
		}
		if (write_str(LUN, dev) != 0) {
			perror("share books");
			mount(dev, dir, "vfat", MS_NOATIME,
			      "utf8,shortname=mixed");
			return -1;
		}
		sharing = 1;
		return 0;
	}
	write_str(LUN, "\n");
	sharing = 0;
	if (mount(dev, dir, "vfat", MS_NOATIME, "utf8,shortname=mixed")
	    != 0) {
		perror("mount books");
		return -1;
	}
	return 0;
}

static int fb_init(int *width, int *height)
{
	native = getenv("MYNOOK") != NULL;
	if (open_screen() != 0)
		return -1;
	open_inputs();
	*width = pw;
	*height = ph;
	return 0;
}

static void fb_shutdown(void)
{
	for (int i = 0; i < n_inputs; i++)
		close(inputs[i].fd);
	if (fb_mem)
		munmap(fb_mem, fb_len);
	if (fb_fd >= 0)
		close(fb_fd);
}

const mr_platform mr_platform_fb = {
	.name = "fb",
	.init = fb_init,
	.present = fb_present,
	.next_event = fb_next_event,
	.shutdown = fb_shutdown,
	.light = fb_light,
	.battery = fb_battery,
	.sleep = fb_sleep,
	.power = fb_power,
	.set_time = fb_set_time,
	.usb_state = fb_usb_state,
	.usb_share = fb_usb_share,
};
