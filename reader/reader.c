/*
 * The reader: MyNook's one program, and a Mac app for trying it out.
 *
 * Usage: ./reader [--script] <books folder> [state file]
 *
 * Turns on into the book you were reading, on the page you left. The
 * n button (Esc on the Mac) always goes Home; from there the tabs
 * along the bottom lead to the Library and Settings.
 *
 * This file: start-up, turning raw finger events into taps, drags and
 * swipes, and the loop that draws a frame after every change. The
 * screens live in home.c, library.c, settings.c, book.c (with the
 * reading menu and Contents), lookup.c (the dictionary) and system.c
 * (lock screen, power, clock, USB).
 *
 * The state file defaults to ~/.myreader-state. The dictionary is
 * found through $MR_DICT, or a "dict" folder beside the program or
 * its folder.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "app.h"

/*
 * A finger that wanders less than SLOP pixels before lifting was a
 * tap; more, and it was a drag. A drag that's mostly sideways and at
 * least SWIPE pixels long is a swipe.
 */
#define SLOP 16
#define SWIPE 80

/* See step 01: swallow MuPDF's per-chapter CSS complaints. */
static void ignore_message(void *user, const char *message)
{
	(void)user;
	(void)message;
}

/* ---- drawing ---- */

/*
 * Dark mode: remap every gray level through a table. White paper
 * (255) becomes near-black (20) and black text (0) becomes soft light
 * gray (225); pure white on pure black glares on an LCD at night.
 * Pictures in a book's pages get inverted too; covers on Home don't,
 * because they're drawn after this.
 */
void apply_dark(struct app *a)
{
	static unsigned char table[256];
	static int built;
	if (!built) {
		for (int v = 0; v < 256; v++)
			table[v] = (unsigned char)(20 + (255 - v) * 205 / 255);
		built = 1;
	}

	int stride = fz_pixmap_stride(a->ctx, a->pix);
	int bytes = a->w * fz_pixmap_components(a->ctx, a->pix);
	unsigned char *s = fz_pixmap_samples(a->ctx, a->pix);
	for (int y = 0; y < a->h; y++) {
		unsigned char *row = s + y * stride;
		for (int x = 0; x < bytes; x++)
			row[x] = table[row[x]];
	}
}

/*
 * Draw screen m into the frame. Boxes that sit over another screen
 * (the power menu, the USB question) call this for what's under them.
 */
void draw_screen(struct app *a, enum mode m)
{
	switch (m) {
	case MODE_HOME:
		draw_home(a);
		break;
	case MODE_LIBRARY:
		draw_library(a);
		break;
	case MODE_SETTINGS:
		draw_settings(a);
		break;
	case MODE_BOOK:
		if (a->doc)
			draw_book(a);
		else
			draw_home(a);
		break;
	case MODE_MENU:
		draw_menu(a);
		break;
	case MODE_CONTENTS:
		draw_contents(a);
		break;
	case MODE_DICT:
		draw_dict(a);
		break;
	case MODE_LOCK:
		draw_lock(a);
		break;
	case MODE_POWER:
		draw_power(a);
		break;
	case MODE_CLOCK:
		draw_clock(a);
		break;
	case MODE_USB:
		draw_usb(a);
		break;
	case MODE_PASS:
		draw_pass(a);
		break;
	}
}

void render_frame(struct app *a)
{
	fz_try(a->ctx) {
		draw_screen(a, a->mode);
		ui_toast(a);
		if (a->lib.set.dark) {
			apply_dark(a);
			/* a book's photos: back to their own colors */
			redraw_pictures(a, page_showing(a));
		}
		/* covers keep their own colors: after the remap */
		if (a->mode == MODE_HOME)
			draw_home_covers(a);
	}
	fz_catch(a->ctx)
		fprintf(stderr, "draw: %s\n", fz_caught_message(a->ctx));

	a->plat->present(fz_pixmap_samples(a->ctx, a->pix),
			 fz_pixmap_stride(a->ctx, a->pix));
	a->status[0] = 0;

	/* the minute on screen now, whether or not this screen shows it */
	struct tm tm;
	time_t t = app_time(a);
	localtime_r(&t, &tm);
	a->shown_minute = tm.tm_hour * 60 + tm.tm_min;
}

/* ---- gestures ---- */

/*
 * Turn one raw event into what it meant. Returns 1 and fills act
 * when there's something to act on; finger-down, and moves too small
 * to be a drag yet, return 0.
 */
static int recognize(struct app *a, const mr_event *ev,
		     struct action *act)
{
	memset(act, 0, sizeof *act);
	switch (ev->type) {
	case MR_EV_HOME:
		act->type = ACT_HOME;
		return 1;
	case MR_EV_NEXT:
		act->type = ACT_NEXT;
		return 1;
	case MR_EV_PREV:
		act->type = ACT_PREV;
		return 1;
	case MR_EV_SCROLL:
		act->type = ACT_SCROLL;
		act->d = ev->y;
		return 1;

	case MR_EV_DOWN:
		a->down = 1;
		a->dragging = 0;
		a->held = 0;
		a->down_ms = app_now(a);
		a->x0 = ev->x;
		a->y0 = ev->y;
		a->last_y = ev->y;
		return 0;

	case MR_EV_TIMEOUT:
		/*
		 * Other timers (the clock, dimming) time out too, so check
		 * that the finger really has been still long enough.
		 */
		if (!a->down || a->dragging || a->held ||
		    app_now(a) - a->down_ms < HOLD_MS)
			return 0;
		a->held = 1;
		act->type = ACT_HOLD;
		act->x = a->x0;
		act->y = a->y0;
		return 1;

	case MR_EV_MOVE:
		if (!a->down || a->held)
			return 0;
		if (!a->dragging && (abs(ev->x - a->x0) > SLOP ||
				     abs(ev->y - a->y0) > SLOP))
			a->dragging = 1;
		if (!a->dragging)
			return 0;
		/*
		 * Lists follow the finger: drag up to go down (d). The
		 * lock screen's slider wants where the finger is (x, y).
		 */
		act->type = ACT_DRAG;
		act->x = ev->x;
		act->y = ev->y;
		act->d = a->last_y - ev->y;
		a->last_y = ev->y;
		return 1;

	case MR_EV_UP: {
		if (!a->down)
			return 0;
		a->down = 0;
		if (a->held)	/* the hold already happened */
			return 0;
		int dx = ev->x - a->x0, dy = ev->y - a->y0;
		if (!a->dragging) {
			act->type = ACT_TAP;
			act->x = a->x0;
			act->y = a->y0;
			return 1;
		}
		if (abs(dx) >= SWIPE && abs(dx) > abs(dy)) {
			/* swipe left: the next page comes in from the right */
			act->type = ACT_SWIPE;
			act->d = dx < 0 ? +1 : -1;
			return 1;
		}
		act->type = ACT_RELEASE;
		act->x = ev->x;
		act->y = ev->y;
		return 1;
	}

	default:
		return 0;
	}
}

/* Hand an action to the screen showing. Returns 1 if it changed. */
static int dispatch(struct app *a, const struct action *act)
{
	/*
	 * The n button: Home from anywhere, except past the lock screen
	 * or while the books are away on a computer.
	 */
	if (act->type == ACT_HOME && a->mode != MODE_LOCK &&
	    !(a->mode == MODE_PASS && a->pass_purpose == PASS_UNLOCK) &&
	    !(a->mode == MODE_USB && a->usb_sharing)) {
		if (a->mode == MODE_HOME)
			return 0;
		ui_go(a, MODE_HOME);
		return 1;
	}
	switch (a->mode) {
	case MODE_HOME:
		return on_home(a, act);
	case MODE_LIBRARY:
		return on_library(a, act);
	case MODE_SETTINGS:
		return on_settings(a, act);
	case MODE_BOOK:
		return on_book(a, act);
	case MODE_MENU:
		return on_menu(a, act);
	case MODE_CONTENTS:
		return on_contents(a, act);
	case MODE_DICT:
		return on_dict(a, act);
	case MODE_LOCK:
		return on_lock(a, act);
	case MODE_POWER:
		return on_power(a, act);
	case MODE_CLOCK:
		return on_clock(a, act);
	case MODE_USB:
		return on_usb(a, act);
	case MODE_PASS:
		return on_pass(a, act);
	}
	return 0;
}

/*
 * How long to wait for the next event: until the soonest of
 * system.c's timers, or a finger resting on a page becoming a hold
 * HOLD_MS after it touched. That deadline counts from the touch, so
 * small jitters (which the Nook's touchscreen sends constantly)
 * don't restart it.
 */
static int wait_ms(struct app *a)
{
	int w = sys_wait_ms(a);
	if (a->down && !a->dragging && !a->held &&
	    (a->mode == MODE_BOOK || a->mode == MODE_DICT)) {
		long long left = a->down_ms + HOLD_MS - app_now(a);
		int h = left > 0 ? (int)left : 0;
		if (w < 0 || h < w)
			w = h;
	}
	return w;
}

/*
 * Where the dictionary lives: $MR_DICT if set, else a "dict" folder
 * next to the program (the Nook), else one beside its folder (the
 * Mac: app/reader finds MyReader/dict).
 */
static mr_dict *open_dict(const char *argv0)
{
	const char *env = getenv("MR_DICT");
	if (env)
		return mr_dict_open(env);

	char dir[1024], path[1100];
	snprintf(dir, sizeof dir, "%s", argv0);
	char *slash = strrchr(dir, '/');
	if (slash)
		*slash = '\0';
	else
		snprintf(dir, sizeof dir, ".");

	snprintf(path, sizeof path, "%s/dict", dir);
	mr_dict *d = mr_dict_open(path);
	if (!d) {
		snprintf(path, sizeof path, "%s/../dict", dir);
		d = mr_dict_open(path);
	}
	return d;
}

int main(int argc, char **argv)
{
	struct app a;
	memset(&a, 0, sizeof a);

	a.plat = &MR_DEFAULT_PLATFORM;
	int arg = 1;
	if (argc > 1 && strcmp(argv[1], "--script") == 0) {
		a.plat = &mr_platform_script;
		arg++;
	}
	if (argc - arg < 1 || argc - arg > 2) {
		fprintf(stderr,
			"usage: %s [--script] <books folder> [state file]\n",
			argv[0]);
		return 1;
	}
	const char *books_dir = argv[arg];
	char state_path[1024];
	if (argc - arg == 2) {
		snprintf(state_path, sizeof state_path, "%s",
			 argv[arg + 1]);
	} else {
		const char *home = getenv("HOME");
		snprintf(state_path, sizeof state_path,
			 "%s/.myreader-state", home ? home : ".");
	}

	if (a.plat->init(&a.w, &a.h) != 0)
		return 1;

	a.ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (!a.ctx) {
		fprintf(stderr, "cannot create MuPDF context\n");
		a.plat->shutdown();
		return 1;
	}
	fz_set_warning_callback(a.ctx, ignore_message, NULL);
	fz_set_error_callback(a.ctx, ignore_message, NULL);

	int status = 0;
	fz_try(a.ctx) {
		fz_register_document_handlers(a.ctx);
		mr_draw_init(a.ctx);
		a.pix = mr_new_pixmap(a.ctx, a.w, a.h);
	}
	fz_catch(a.ctx) {
		fprintf(stderr, "error: %s\n", fz_caught_message(a.ctx));
		status = 1;
	}

	if (status == 0) {
		a.books_dir = books_dir;
		a.wall0 = time(NULL);
		a.now0 = a.last_input = app_now(&a);
		a.light_before = -1;
		mr_library_load(&a.lib, state_path);
		mr_library_scan(a.ctx, &a.lib, books_dir);
		mr_library_sort(&a.lib);
		mr_library_save(&a.lib);	/* keep the titles read */
		apply_settings(&a);
		if (a.lib.set.light >= 0 && a.plat->light)
			a.plat->light(a.lib.set.light);
		a.dict = open_dict(argv[0]);
		printf("%d books in %s, state in %s, %s backend, %s\n",
		       a.lib.seen_count, books_dir, state_path,
		       a.plat->name,
		       a.dict ? "dictionary" : "no dictionary");

		/* A new MyNook waiting? Install it before anything else. */
		sys_update(&a);

		/* Turn on into the book you were reading. */
		ui_go(&a, MODE_HOME);
		mr_book *last;
		if (mr_library_recent(&a.lib, &last, 1))
			open_book(&a, last);

		/*
		 * The loop: draw if something changed (never while
		 * asleep), wait for an event or a timer, let system.c
		 * look first (power button, dimming, sleep), then turn
		 * what's left into a gesture for the screen showing.
		 */
		int dirty = 1;
		while (!a.quit) {
			if (dirty && !a.asleep)
				render_frame(&a);
			mr_event ev;
			a.plat->next_event(&ev, wait_ms(&a));
			if (ev.type == MR_EV_QUIT)
				break;
			int used = sys_event(&a, &ev);
			dirty = used == 1;
			if (!used) {
				struct action act;
				dirty = recognize(&a, &ev, &act) &&
					dispatch(&a, &act);
			}
			dirty |= sys_tick(&a);
		}
	}

	close_dict(&a);
	mr_dict_close(a.dict);
	close_book(&a);
	drop_covers(&a);
	mr_library_free(&a.lib);
	fz_drop_pixmap(a.ctx, a.library.pix);
	fz_drop_pixmap(a.ctx, a.pix);
	mr_draw_done(a.ctx);
	fz_drop_context(a.ctx);
	a.plat->shutdown();
	return status;
}
