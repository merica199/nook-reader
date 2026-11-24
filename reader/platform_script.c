/*
 * Backend: no screen and no person. Events come from stdin, one per
 * line, and every frame the reader shows is saved as frame-NNN.ppm.
 * It lets the reader be tested without anyone clicking, on the Mac
 * and under QEMU.
 *
 *   n        next page
 *   p        previous page
 *   t X Y    tap at X, Y (finger down, then up, same spot)
 *   d X Y    finger down
 *   m X Y    finger moved
 *   u X Y    finger up
 *   w DY     mouse wheel, DY pixels (+ = down the list)
 *   h        home button
 *   .        time passes until the reader's next timer (the script's
 *            clock jumps ahead; nothing waits): "d X Y", ".",
 *            "u X Y" is a press and hold; with nothing touched,
 *            each "." reaches the next minute, dimming or sleep
 *   o        power button pressed and released
 *   O        power button held (pressed, time passes, released)
 *   U        plug into a computer, or unplug
 *   C        plug in a charger, or unplug it
 *   E        eject the shared drive on the computer
 *   q        quit (so does the end of input)
 *
 * The battery reads 76%. Sleeping, waking, setting the clock and
 * sharing over USB are printed instead of done.
 */
#include <stdio.h>
#include "platform.h"

static int frame;

static int script_init(int *width, int *height)
{
	*width = MR_NOOK_W;
	*height = MR_NOOK_H;
	return 0;
}

static void script_present(const unsigned char *rgb, int stride)
{
	char name[32];
	snprintf(name, sizeof name, "frame-%03d.ppm", frame++);

	FILE *f = fopen(name, "wb");
	if (!f) {
		perror(name);
		return;
	}
	/*
	 * PPM, the simplest color image format there is: a short text
	 * header, then the raw RGB bytes row by row. Rows are written one
	 * at a time because the stride may be wider than the visible
	 * width.
	 */
	fprintf(f, "P6\n%d %d\n255\n", MR_NOOK_W, MR_NOOK_H);
	for (int y = 0; y < MR_NOOK_H; y++)
		fwrite(rgb + y * stride, 1, MR_NOOK_W * 3, f);
	fclose(f);
	printf("wrote %s\n", name);
}

/* One line can stand for several events; the rest wait here. */
static mr_event pending[4];
static int n_pending, next_pending;

static void later(mr_event_type type, int x, int y)
{
	if (n_pending < 4)
		pending[n_pending++] = (mr_event){ type, x, y };
}

static int plugged, shared, charger;

/*
 * The script's own clock. Nothing waits: a timeout ("." or the one
 * inside "O") moves the clock on by exactly what the reader asked
 * to wait, as if that long had passed.
 */
static long long clock_ms = 1000000;

static long long script_now_ms(void)
{
	return clock_ms;
}

static void timeout(mr_event *ev, int timeout_ms)
{
	ev->type = MR_EV_TIMEOUT;
	if (timeout_ms > 0)
		clock_ms += timeout_ms;
}

static void script_next_event(mr_event *ev, int timeout_ms)
{
	char line[64];

	if (next_pending < n_pending) {
		*ev = pending[next_pending++];
		if (ev->type == MR_EV_TIMEOUT)
			timeout(ev, timeout_ms);
		return;
	}
	n_pending = next_pending = 0;

	while (fgets(line, sizeof line, stdin)) {
		int x = 0, y = 0;
		int two = sscanf(line + 1, "%d %d", &x, &y) == 2;
		ev->x = x;
		ev->y = y;

		switch (line[0]) {
		case 'n':
			ev->type = MR_EV_NEXT;
			return;
		case 'p':
			ev->type = MR_EV_PREV;
			return;
		case 't':
		case 'd':
		case 'm':
		case 'u':
			if (!two) {
				fprintf(stderr, "bad line: %s", line);
				break;
			}
			if (line[0] == 't') {
				ev->type = MR_EV_DOWN;
				later(MR_EV_UP, x, y);
			} else {
				ev->type = line[0] == 'd' ? MR_EV_DOWN :
					   line[0] == 'm' ? MR_EV_MOVE :
							    MR_EV_UP;
			}
			return;
		case 'w':
			if (sscanf(line + 1, "%d", &y) == 1) {
				ev->type = MR_EV_SCROLL;
				ev->x = 0;
				ev->y = y;
				return;
			}
			break;
		case 'h':
			ev->type = MR_EV_HOME;
			return;
		case '.':
			timeout(ev, timeout_ms);
			return;
		case 'o':
			ev->type = MR_EV_POWER;
			later(MR_EV_POWER_UP, 0, 0);
			return;
		case 'O':
			ev->type = MR_EV_POWER;
			later(MR_EV_TIMEOUT, 0, 0);
			later(MR_EV_POWER_UP, 0, 0);
			return;
		case 'U':
			plugged = !plugged;
			if (!plugged)
				shared = 0;
			printf("usb: %s\n", plugged ? "plugged" : "unplugged");
			ev->type = MR_EV_TIMEOUT;	/* let the reader look */
			return;
		case 'C':
			charger = !charger;
			printf("charger: %s\n", charger ? "in" : "out");
			ev->type = MR_EV_CHARGER;
			return;
		case 'E':
			shared = 0;
			printf("usb: ejected\n");
			ev->type = MR_EV_TIMEOUT;
			return;
		case 'q':
			ev->type = MR_EV_QUIT;
			return;
		}
	}
	ev->type = MR_EV_QUIT;
}

static void script_shutdown(void)
{
}

static int script_battery(int *charging)
{
	*charging = plugged || charger;
	return 76;
}

static void script_sleep(int on)
{
	printf("%s\n", on ? "wake" : "sleep");
}

static void script_power(int how)
{
	printf("power: %s\n", how == MR_POWER_OFF ? "off" : "restart");
}

static int script_set_time(time_t t)
{
	char buf[32];
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
	printf("clock: %s\n", buf);
	return 0;
}

static int script_usb_state(void)
{
	return !plugged ? MR_USB_NONE :
	       shared ? MR_USB_SHARING : MR_USB_COMPUTER;
}

static int script_usb_share(int on)
{
	shared = on && plugged;
	printf("usb: %s\n", on ? "sharing" : "taken back");
	return 0;
}

const mr_platform mr_platform_script = {
	.name = "script",
	.init = script_init,
	.present = script_present,
	.next_event = script_next_event,
	.shutdown = script_shutdown,
	.battery = script_battery,
	.sleep = script_sleep,
	.power = script_power,
	.set_time = script_set_time,
	.usb_state = script_usb_state,
	.usb_share = script_usb_share,
	.now_ms = script_now_ms,
};
