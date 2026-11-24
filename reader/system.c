/*
 * The device side of the reader: dimming and sleep, the lock screen,
 * the power button and its menu, setting the clock, and sharing the
 * books with a computer over USB.
 *
 * Awake, then (no input for "Sleep after" less 10 seconds) dimmed,
 * as a warning, then asleep. The power button: a press sleeps, or wakes;
 * a hold opens the power menu. Waking shows the lock screen; sliding
 * its knob across goes back to where you were. The lock screen goes
 * dark again 5 seconds after it lit up or was last touched.
 *
 * While its drive is connected to a computer, the Nook dims but
 * doesn't sleep by itself: asleep, the USB connection drops, and the
 * computer would see its drive vanish mid-copy. On a charger, or
 * plugged into a computer with "Charge only", it dims and sleeps as
 * usual.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"

#define POWER_HOLD_MS 1500	/* a hold, not a press: the power menu */
/*
 * The lock screen goes dark again this long after it lit up or was
 * last touched, however it was woken (power button, n, plugging in).
 */
#define LOCK_SHOW_MS 5000
#define USB_POLL_MS 2000	/* how often to look at the USB port */

/* ---- light, sleep, waking ---- */

static int light_now(struct app *a)
{
	return a->plat->light ? a->plat->light(-1) : -1;
}

static void undim(struct app *a)
{
	if (a->dimmed && a->light_before >= 0)
		a->plat->light(a->light_before);
	a->dimmed = 0;
}

/* The lock screen, or the passcode keypad that follows it. */
static int locked(struct app *a)
{
	return a->mode == MODE_LOCK ||
	       (a->mode == MODE_PASS && a->pass_purpose == PASS_UNLOCK);
}

void sys_sleep(struct app *a)
{
	if (a->asleep)
		return;
	if (locked(a)) {
		a->mode = MODE_LOCK;	/* unlock_to stays as it was */
	} else {
		/* from the power menu, go back to what it covered */
		a->unlock_to = a->mode == MODE_POWER ? a->menu_over
						     : a->mode;
		a->mode = MODE_LOCK;
	}
	if (!a->dimmed)
		a->light_before = light_now(a);
	a->dimmed = 0;
	a->asleep = 1;
	a->slide = a->sliding = 0;
	a->down = 0;
	if (a->plat->sleep)
		a->plat->sleep(0);
}

static void wake(struct app *a)
{
	if (a->plat->sleep)
		a->plat->sleep(1);
	a->asleep = 0;
	if (a->plat->light && a->light_before >= 0)
		a->plat->light(a->light_before);
	a->last_input = app_now(a);
	a->lock_until = a->last_input + LOCK_SHOW_MS;
	a->slide = a->sliding = 0;
}

static void open_power_menu(struct app *a)
{
	if (a->mode == MODE_POWER)
		return;
	a->menu_over = a->mode;
	a->mode = MODE_POWER;
}

/* ---- events and timers (called from reader.c's loop) ---- */

/*
 * Look at an event before the gestures do. Returns 0 to pass it on,
 * 1 if it was used here and the screen changed, 2 if it was used
 * and nothing needs drawing.
 */
int sys_event(struct app *a, const mr_event *ev)
{
	long long now = app_now(a);

	switch (ev->type) {
	case MR_EV_POWER:
		a->last_input = now;
		if (a->asleep) {
			wake(a);
			return 1;
		}
		undim(a);
		a->power_down = 1;
		a->power_at = now;
		a->power_long = 0;
		return 2;

	case MR_EV_POWER_UP:
		if (!a->power_down)
			return 2;	/* the release of a waking press */
		a->power_down = 0;
		if (a->power_long)
			return 2;	/* the menu is already up */
		if (a->usb_sharing) {
			snprintf(a->status, sizeof a->status,
				 "Eject the drive on the computer first");
			return 1;
		}
		sys_sleep(a);
		return 2;

	case MR_EV_CHARGER:
		/*
		 * Plugging in or out lights the screen for a moment, like a
		 * phone, to show it took.
		 */
		a->last_input = now;
		if (a->asleep) {
			wake(a);
			return 1;
		}
		if (locked(a))
			a->lock_until = now + LOCK_SHOW_MS;
		return 1;	/* the battery icon changed */

	case MR_EV_TIMEOUT:
		if (a->power_down && !a->power_long &&
		    now - a->power_at >= POWER_HOLD_MS) {
			a->power_long = 1;
			open_power_menu(a);
			return 1;
		}
		return 0;	/* maybe a press and hold on a word */

	default:
		break;
	}

	if (a->asleep) {
		/* the n button wakes too, if the kernel let it */
		if (ev->type == MR_EV_HOME) {
			wake(a);
			return 1;
		}
		return 2;	/* touches on a dark screen do nothing */
	}
	if (ev->type == MR_EV_QUIT)
		return 0;

	a->last_input = now;
	if (a->dimmed) {
		/*
		 * This touch or button only brightens the screen. A touch
		 * swallowed at its start leaves its move and lift
		 * meaningless to the gestures, which never saw it begin.
		 */
		undim(a);
		return 2;
	}
	if (locked(a))
		a->lock_until = now + LOCK_SHOW_MS;
	return 0;
}

/* ---- updates ---- */

/* A plain screen with a message, shown at once, for slow jobs. */
static void show_message(struct app *a, const char *title,
			 const char *body)
{
	fz_context *ctx = a->ctx;
	fz_clear_pixmap_with_value(ctx, a->pix, MR_WHITE);
	mr_text(ctx, a->pix, a->w / 2, a->h / 2 - 40, title, MR_BOLD, 32,
		MR_BLACK, a->w - 2 * MARGIN, MR_CENTER);
	if (body)
		mr_text(ctx, a->pix, a->w / 2, a->h / 2 + 10, body,
			MR_REGULAR, 22, MR_GRAY, a->w - 2 * MARGIN,
			MR_CENTER);
	if (a->lib.set.dark)
		apply_dark(a);
	a->plat->present(fz_pixmap_samples(ctx, a->pix),
			 fz_pixmap_stride(ctx, a->pix));
}

/*
 * Install a waiting update, if there is one, and restart into it. At
 * start-up and whenever the books come back from a computer.
 */
void sys_update(struct app *a)
{
	char msg[160] = "";
	if (!a->plat->update || a->plat->update(0, msg, sizeof msg) != 1)
		return;

	show_message(a, "Updating MyNook", "Don't turn the Nook off.");
	int r = a->plat->update(1, msg, sizeof msg);
	if (r == 1) {
		show_message(a, "MyNook updated", "Restarting...");
		mr_library_save(&a->lib);
		if (a->plat->power)
			a->plat->power(MR_POWER_RESTART);
		a->quit = 1;	/* still here: this device can't restart */
		return;
	}
	if (r < 0) {
		fprintf(stderr, "update: %s\n", msg);
		snprintf(a->status, sizeof a->status, "%s", msg);
	}
}

/* The books came back from the computer: look at them again. */
static void books_back(struct app *a)
{
	a->usb_sharing = 0;
	drop_covers(a);
	mr_library_scan(a->ctx, &a->lib, a->books_dir);
	mr_library_sort(&a->lib);
	mr_library_save(&a->lib);
	ui_go(a, MODE_HOME);
	snprintf(a->status, sizeof a->status, "%d books in your library",
		 a->lib.seen_count);
	sys_update(a);	/* a new MyNook may have come with the books */
}

static int usb_tick(struct app *a)
{
	int st = a->plat->usb_state();
	int was = a->usb;
	/*
	 * Plugged into a computer while locked: ask once it's unlocked,
	 * not never. Leaving a->usb as it was makes the next look after
	 * unlocking see it as new.
	 */
	if (st == MR_USB_COMPUTER && was == MR_USB_NONE && locked(a))
		return 0;
	a->usb = st;

	if (a->usb_sharing && st != MR_USB_SHARING) {
		/* ejected on the computer, or unplugged */
		if (a->plat->usb_share(0) != 0)
			fprintf(stderr, "usb: taking the books back failed\n");
		books_back(a);
		return 1;
	}
	if (st == MR_USB_COMPUTER && was == MR_USB_NONE && !locked(a) &&
	    a->mode != MODE_POWER) {
		a->menu_over = a->mode;
		a->mode = MODE_USB;
		return 1;
	}
	if (st == MR_USB_NONE && a->mode == MODE_USB) {
		a->mode = a->menu_over;
		return 1;
	}
	return 0;
}

/* Called once per trip round the loop. Returns 1 to redraw. */
int sys_tick(struct app *a)
{
	if (a->asleep)
		return 0;

	int dirty = 0;
	if (a->plat->usb_state)
		dirty |= usb_tick(a);

	long long now = app_now(a);
	long long idle = now - a->last_input;
	long long dim = setting_dim_ms(a), slp = setting_sleep_ms(a);
	/*
	 * A finger or the power button down: nothing happens by itself.
	 * The drive connected to a computer: it may dim, but never sleep,
	 * which would drop the USB link mid-copy.
	 */
	int busy = a->down || a->power_down;
	int no_sleep = busy || a->usb_sharing;

	if (locked(a)) {
		if (now >= a->lock_until && !no_sleep) {
			sys_sleep(a);
			return 0;
		}
	} else if (!busy) {
		if (slp && idle >= slp && !no_sleep) {
			sys_sleep(a);
			return 0;
		}
		if (dim && !a->dimmed && idle >= dim) {
			int level = light_now(a);
			if (level >= 0) {
				a->light_before = level;
				a->plat->light(level / 3);
				a->dimmed = 1;
			}
		}
	}

	/* the clock on screen: redraw when the minute turns */
	struct tm tm;
	time_t t = app_time(a);
	localtime_r(&t, &tm);
	if (tm.tm_hour * 60 + tm.tm_min != a->shown_minute)
		dirty = 1;
	return dirty;
}

/*
 * How long until the next timer: the power button becoming a hold,
 * dimming, sleep, the lock screen going dark, the minute turning,
 * the next look at the USB port. -1: nothing pending.
 */
int sys_wait_ms(struct app *a)
{
	long long now = app_now(a), next = LLONG_MAX;
#define SOON(t) do { if ((t) < next) next = (t); } while (0)

	if (a->power_down && !a->power_long)
		SOON(a->power_at + POWER_HOLD_MS);
	if (!a->asleep) {
		/* the same rules as sys_tick */
		int busy = a->down || a->power_down;
		int no_sleep = busy || a->usb_sharing;
		long long dim = setting_dim_ms(a);
		long long slp = setting_sleep_ms(a);
		if (locked(a)) {
			if (!no_sleep)
				SOON(a->lock_until);
		} else if (!busy) {
			if (dim && !a->dimmed && light_now(a) >= 0)
				SOON(a->last_input + dim);
			if (slp && !no_sleep)
				SOON(a->last_input + slp);
		}
		time_t t = app_time(a);
		SOON(now + (60 - t % 60) * 1000LL);
		/*
		 * The USB port has no event of its own, so look every
		 * couple of seconds. (The script backend, with its own
		 * clock, says when it's plugged in, and polling would
		 * only shrink its "." steps to two seconds.)
		 */
		if (a->plat->usb_state && !a->plat->now_ms)
			SOON(now + USB_POLL_MS);
	}
#undef SOON
	if (next == LLONG_MAX)
		return -1;
	if (next <= now)
		return 0;
	return next - now > INT_MAX ? INT_MAX : (int)(next - now);
}

/* ---- the lock screen ---- */

/*
 * Time, date, battery and the slider; nothing about what you're
 * reading.
 */
#define TIME_Y 330	/* the time's baseline */
#define KNOB 96
#define TRACK_H (KNOB + 8)
#define TRACK_X 40

static int track_y(struct app *a)
{
	return a->h - 64 - TRACK_H;
}

static int track_w(struct app *a)
{
	return a->w - 2 * TRACK_X;
}

static int slide_max(struct app *a)
{
	return track_w(a) - KNOB - 8;
}

void draw_lock(struct app *a)
{
	fz_context *ctx = a->ctx;
	fz_pixmap *pix = a->pix;
	char buf[64];
	time_t now = app_time(a);
	struct tm tm;
	localtime_r(&now, &tm);

	fz_clear_pixmap_with_value(ctx, pix, MR_WHITE);

	int charging = 0;
	int pct = a->plat->battery ? a->plat->battery(&charging) : -1;

	/* the time, big; AM or PM beside it, small */
	char tod[16], ampm[4] = "";
	if (a->lib.set.clock24) {
		strftime(tod, sizeof tod, "%H:%M", &tm);
	} else {
		int h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
		snprintf(tod, sizeof tod, "%d:%02d", h, tm.tm_min);
		snprintf(ampm, sizeof ampm, "%s",
			 tm.tm_hour < 12 ? "AM" : "PM");
	}
	float tw = mr_text_width(ctx, tod, MR_BOLD, 150);
	float aw = ampm[0] ? 12 + mr_text_width(ctx, ampm, MR_BOLD, 40) : 0;
	float x0 = (a->w - tw - aw) / 2;
	mr_text(ctx, pix, x0, TIME_Y, tod, MR_BOLD, 150, MR_BLACK, 0,
		MR_LEFT);
	if (ampm[0])
		mr_text(ctx, pix, x0 + tw + 12, TIME_Y, ampm, MR_BOLD, 40,
			MR_BLACK, 0, MR_LEFT);

	/* "Tuesday, November 24" */
	char day[16], month[16];
	strftime(day, sizeof day, "%A", &tm);
	strftime(month, sizeof month, "%B", &tm);
	snprintf(buf, sizeof buf, "%s, %s %d", day, month, tm.tm_mday);
	mr_text(ctx, pix, a->w / 2, TIME_Y + 60, buf, MR_REGULAR, 30,
		MR_GRAY, 0, MR_CENTER);

	/* the battery, big, with "Charging" while it is */
	if (pct >= 0) {
		snprintf(buf, sizeof buf, "%s%d%%",
			 charging ? "Charging, " : "", pct);
		int bw = 96, bh = 44, gap = 18;
		float tw2 = mr_text_width(ctx, buf, MR_REGULAR, 28);
		int bx = (int)((a->w - bw - 6 - gap - tw2) / 2);
		int by = TIME_Y + 140;
		ui_battery(a, bx, by, bw, bh, pct, charging, MR_BLACK);
		mr_text(ctx, pix, bx + bw + 6 + gap, by + bh / 2 + 10, buf,
			MR_REGULAR, 28, MR_BLACK, 0, MR_LEFT);
	}

		if (pct >= 0 && pct < 15 && !charging)
		mr_text(ctx, pix, a->w / 2, track_y(a) - 24,
			"Battery low: plug in soon", MR_BOLD, 22, MR_BLACK,
			0, MR_CENTER);

	/* the slider: a track with a knob to drag to the far end */
	int ty = track_y(a), tw2 = track_w(a);
	mr_outline(ctx, pix, TRACK_X, ty, tw2, TRACK_H, 2, MR_BLACK);
	if (a->slide == 0)
		mr_text(ctx, pix, TRACK_X + KNOB + 4 + (tw2 - KNOB - 4) / 2,
			ty + TRACK_H / 2 + 9, "Slide to unlock", MR_REGULAR,
			26, MR_GRAY, 0, MR_CENTER);
	int kx = TRACK_X + 4 + a->slide;
	mr_fill(ctx, pix, kx, ty + 4, KNOB, KNOB, MR_BLACK);
	mr_text(ctx, pix, kx + KNOB / 2, ty + 4 + KNOB / 2 + 14, ">",
		MR_BOLD, 44, MR_WHITE, 0, MR_CENTER);
}

/* Past the slider: the passcode if there is one, else straight in. */
static void unlock(struct app *a)
{
	a->slide = a->sliding = 0;
	if (a->lib.set.pass)
		pass_unlock(a);
	else
		unlock_done(a);
}

/* Back to where you were. */
void unlock_done(struct app *a)
{
	enum mode to = a->unlock_to;
	if (to == MODE_LOCK || to == MODE_POWER || to == MODE_USB ||
	    to == MODE_PASS)
		to = MODE_HOME;
	if (to == MODE_BOOK && !a->doc)
		to = MODE_HOME;
	a->mode = to;
	a->slide = a->sliding = 0;
}

int on_lock(struct app *a, const struct action *act)
{
	int ty = track_y(a);
	switch (act->type) {
	case ACT_DRAG:
		if (!a->sliding) {
			/* only a drag that starts on the knob moves it */
			int kx = TRACK_X + 4 + a->slide;
			if (a->x0 < kx - 20 || a->x0 > kx + KNOB + 20 ||
			    a->y0 < ty - 20 || a->y0 > ty + TRACK_H + 20)
				return 0;
			a->sliding = 1;
		}
		a->slide = act->x - a->x0;
		if (a->slide < 0)
			a->slide = 0;
		if (a->slide > slide_max(a))
			a->slide = slide_max(a);
		return 1;
	case ACT_RELEASE:
	case ACT_SWIPE:
		if (!a->sliding)
			return 0;
		/* most of the way counts, for a quick flick */
		if (a->slide >= slide_max(a) * 3 / 4) {
			unlock(a);
			return 1;
		}
		a->slide = a->sliding = 0;
		return 1;
	default:
		return 0;	/* taps don't unlock: pockets tap */
	}
}

/* ---- the power menu ---- */

static const char *power_body(void)
{
	/* only under MyNook is there another system to start */
	return getenv("MYNOOK") ?
	       "Restart starts the Nook's default system. Hold n while "
	       "it starts for the menu, to choose." : NULL;
}

static const char *power_buttons[] = { "Power off", "Restart",
				       "Cancel" };

void draw_power(struct app *a)
{
	if (a->menu_over != MODE_POWER)
		draw_screen(a, a->menu_over);
	ui_dialog(a, "Power", power_body(), power_buttons, 3, 0);
}

int on_power(struct app *a, const struct action *act)
{
	if (act->type == ACT_HOME) {
		a->mode = a->menu_over;
		return 1;
	}
	if (act->type != ACT_TAP)
		return 0;
	int i = ui_dialog_hit(a, power_body(), 3, act->x, act->y);
	if (i == 0 || i == 1) {
		mr_library_save(&a->lib);
		if (a->plat->power)
			a->plat->power(i == 0 ? MR_POWER_OFF
					      : MR_POWER_RESTART);
		/* still here: this device can't, so just stop */
		a->quit = 1;
		return 0;
	}
	a->mode = a->menu_over;		/* Cancel, or a tap outside */
	return 1;
}

/* ---- setting the date and time ---- */

#define CLK_TOP (HEAD_H + 140)
#define CLK_ROW 96
#define CLK_BTN 72

enum { F_YEAR, F_MONTH, F_DAY, F_HOUR, F_MIN, N_FIELDS };
static const char *field_names[N_FIELDS] = {
	"Year", "Month", "Day", "Hour", "Minute",
};

void open_clock(struct app *a)
{
	time_t now = app_time(a);
	localtime_r(&now, &a->clock_edit);
	a->clock_edit.tm_sec = 0;
	a->mode = MODE_CLOCK;
}

static void field_value(struct app *a, int f, char *buf, size_t n)
{
	struct tm *t = &a->clock_edit;
	switch (f) {
	case F_YEAR:
		snprintf(buf, n, "%d", t->tm_year + 1900);
		break;
	case F_MONTH:
		strftime(buf, n, "%B", t);
		break;
	case F_DAY:
		snprintf(buf, n, "%d", t->tm_mday);
		break;
	case F_HOUR:
		if (a->lib.set.clock24)
			snprintf(buf, n, "%02d", t->tm_hour);
		else
			snprintf(buf, n, "%d %s",
				 t->tm_hour % 12 ? t->tm_hour % 12 : 12,
				 t->tm_hour < 12 ? "AM" : "PM");
		break;
	default:
		snprintf(buf, n, "%02d", t->tm_min);
	}
}

/* The two buttons along the bottom: Cancel and Set. */
static void clock_button(struct app *a, int i, int *x, int *y, int *w)
{
	*w = (a->w - 2 * MARGIN - 16) / 2;
	*x = MARGIN + i * (*w + 16);
	*y = a->h - MARGIN - CLK_BTN;
}

void draw_clock(struct app *a)
{
	fz_context *ctx = a->ctx;
	char buf[64];

	fz_clear_pixmap_with_value(ctx, a->pix, MR_WHITE);
	ui_header(a, "Date & time");

	/* the result, as it will read */
	time_t t = mktime(&a->clock_edit);
	ui_date_time(a, t, buf, sizeof buf);
	mr_text(ctx, a->pix, a->w / 2, HEAD_H + 80, buf, MR_BOLD, 26,
		MR_BLACK, a->w - 2 * MARGIN, MR_CENTER);

	for (int f = 0; f < N_FIELDS; f++) {
		int y = CLK_TOP + f * CLK_ROW;
		int by = y + (CLK_ROW - CLK_BTN) / 2;
		mr_text(ctx, a->pix, MARGIN, y + CLK_ROW / 2 + 8,
			field_names[f], MR_BOLD, 22, MR_BLACK, 160,
			MR_LEFT);
		ui_button(a, 190, by, CLK_BTN, CLK_BTN, "-", 0);
		field_value(a, f, buf, sizeof buf);
		mr_text(ctx, a->pix, (190 + CLK_BTN + a->w - MARGIN -
				      CLK_BTN) / 2,
			by + CLK_BTN / 2 + 9, buf, MR_BOLD, 26, MR_BLACK,
			0, MR_CENTER);
		ui_button(a, a->w - MARGIN - CLK_BTN, by, CLK_BTN, CLK_BTN,
			  "+", 0);
		mr_fill(ctx, a->pix, MARGIN, y + CLK_ROW - 1,
			a->w - 2 * MARGIN, 1, MR_LIGHT);
	}

	static const char *labels[] = { "Cancel", "Set" };
	for (int i = 0; i < 2; i++) {
		int x, y, w;
		clock_button(a, i, &x, &y, &w);
		ui_button(a, x, y, w, CLK_BTN, labels[i], i == 1);
	}
}

int on_clock(struct app *a, const struct action *act)
{
	if (act->type == ACT_HOME)
		return 0;	/* the n button already went Home */
	if (act->type != ACT_TAP)
		return 0;
	int x = act->x, y = act->y;

	for (int i = 0; i < 2; i++) {
		int bx, by, bw;
		clock_button(a, i, &bx, &by, &bw);
		if (x < bx || x >= bx + bw || y < by || y >= by + CLK_BTN)
			continue;
		if (i == 1) {
			time_t t = mktime(&a->clock_edit);
			int ok = a->plat->set_time &&
				 a->plat->set_time(t) == 0;
			snprintf(a->status, sizeof a->status, "%s",
				 ok ? "Clock set" : "Couldn't set the clock");
		}
		a->mode = MODE_SETTINGS;
		return 1;
	}

	int f = (y - CLK_TOP) / CLK_ROW;
	if (y < CLK_TOP || f >= N_FIELDS)
		return 0;
	int by = CLK_TOP + f * CLK_ROW + (CLK_ROW - CLK_BTN) / 2;
	if (y < by || y >= by + CLK_BTN)
		return 0;
	int d;
	if (x >= 190 && x < 190 + CLK_BTN)
		d = -1;
	else if (x >= a->w - MARGIN - CLK_BTN && x < a->w - MARGIN)
		d = +1;
	else
		return 0;

	/* mktime carries over: minute 60 is the next hour */
	struct tm *t = &a->clock_edit;
	int *v[N_FIELDS] = { &t->tm_year, &t->tm_mon, &t->tm_mday,
			     &t->tm_hour, &t->tm_min };
	*v[f] += d;
	t->tm_isdst = -1;
	mktime(t);
	return 1;
}

/* ---- USB ---- */

/*
 * Plugged into a computer (a wall charger never asks): connect, so
 * the computer sees the Nook's storage as a drive, for adding books
 * or a MyNook update; or just charge.
 */
static const char *usb_body =
	"Connect to add books or a MyNook update from the computer. "
	"Reading pauses until you eject the Nook there.";
static const char *usb_buttons[] = { "Connect to computer",
				     "Charge only" };

void draw_usb(struct app *a)
{
	fz_context *ctx = a->ctx;
	if (!a->usb_sharing) {
		if (a->menu_over != MODE_USB)
			draw_screen(a, a->menu_over);
		ui_dialog(a, "Connected to a computer", usb_body,
			  usb_buttons, 2, 0);
		return;
	}
	fz_clear_pixmap_with_value(ctx, a->pix, MR_WHITE);
	ui_header(a, "Computer");
	mr_text(ctx, a->pix, a->w / 2, a->h / 2 - 80,
		"Connected to computer", MR_BOLD, 30, MR_BLACK,
		a->w - 2 * MARGIN, MR_CENTER);
	mr_text_wrap(ctx, a->pix, MARGIN + 20, a->h / 2 - 20,
		     "Add books or a MyNook update on the computer, then "
		     "eject the NOOK drive there. Reading picks up again "
		     "as soon as it's ejected or unplugged.", MR_REGULAR,
		     22, MR_GRAY, a->w - 2 * MARGIN - 40, 5, 32);
}

int on_usb(struct app *a, const struct action *act)
{
	if (a->usb_sharing || act->type != ACT_TAP)
		return 0;
	int i = ui_dialog_hit(a, usb_body, 2, act->x, act->y);
	if (i == 0) {
		/* nothing may hold a file in the books folder */
		close_dict(a);
		close_book(a);
		drop_covers(a);
		if (a->plat->usb_share(1) == 0) {
			a->usb_sharing = 1;
			a->usb = MR_USB_SHARING;
		} else {
			snprintf(a->status, sizeof a->status,
				 "Couldn't share the books");
			ui_go(a, MODE_HOME);
		}
		return 1;
	}
	if (i == 1) {
		a->mode = a->menu_over;
		return 1;
	}
	return 0;
}
