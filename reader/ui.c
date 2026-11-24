/*
 * The pieces every screen uses: the title bar, the tab bar along the
 * bottom, buttons, one-off messages, scrolling lists, and moving from
 * one screen to another.
 *
 * Everything is drawn in light colors; dark mode remaps the finished
 * frame (reader.c), so no screen needs two color schemes.
 */
#include <stdio.h>
#include <string.h>
#include "app.h"

/* ---- time ---- */

/* Milliseconds on a clock that never jumps (see now_ms). */
long long app_now(const struct app *a)
{
	if (a->plat->now_ms)
		return a->plat->now_ms();
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The wall clock; under the script backend it follows the script. */
time_t app_time(const struct app *a)
{
	if (!a->plat->now_ms)
		return time(NULL);
	return a->wall0 + (time_t)((app_now(a) - a->now0) / 1000);
}

/* "2:05 PM" or "14:05", by the Clock setting. */
void ui_time(struct app *a, time_t t, char *buf, size_t n)
{
	struct tm tm;
	localtime_r(&t, &tm);
	if (a->lib.set.clock24) {
		strftime(buf, n, "%H:%M", &tm);
	} else {
		int h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
		snprintf(buf, n, "%d:%02d %s", h, tm.tm_min,
			 tm.tm_hour < 12 ? "AM" : "PM");
	}
}

/* "Tue, Nov 24, 2026, 2:05 PM" */
void ui_date_time(struct app *a, time_t t, char *buf, size_t n)
{
	struct tm tm;
	char date[48], tod[16];
	localtime_r(&t, &tm);
	strftime(date, sizeof date, "%a, %b %e, %Y", &tm);
	ui_time(a, t, tod, sizeof tod);
	snprintf(buf, n, "%s, %s", date, tod);
}

/*
 * The time and the battery, right-aligned at right: "2:05 PM   76%".
 * Every screen's title bar has it, and the reading menu.
 */
void ui_status(struct app *a, int right, int baseline, int gray)
{
	char buf[48], tod[16];
	ui_time(a, app_time(a), tod, sizeof tod);
	int charging = 0;
	int pct = a->plat->battery ? a->plat->battery(&charging) : -1;
	if (pct >= 0)
		snprintf(buf, sizeof buf, "%s   %d%%%s", tod, pct,
			 charging ? "+" : "");
	else
		snprintf(buf, sizeof buf, "%s", tod);
	mr_text(a->ctx, a->pix, right, baseline, buf, MR_REGULAR, 20, gray,
		0, MR_RIGHT);
	struct tm tm;
	time_t t = app_time(a);
	localtime_r(&t, &tm);
	a->shown_minute = tm.tm_hour * 60 + tm.tm_min;
}

/* ---- pieces ---- */

/* The title bar, with the time and battery ending at right. */
void ui_header_to(struct app *a, const char *title, int right)
{
	mr_fill(a->ctx, a->pix, 0, 0, a->w, HEAD_H, MR_WHITE);
	mr_text(a->ctx, a->pix, MARGIN, 62, title, MR_BOLD, 34, MR_BLACK,
		right - MARGIN - 190, MR_LEFT);
	ui_status(a, right, 60, MR_GRAY);
	mr_fill(a->ctx, a->pix, 0, HEAD_H - 2, a->w, 2, MR_BLACK);
}

void ui_header(struct app *a, const char *title)
{
	ui_header_to(a, title, a->w - MARGIN);
}

/*
 * A box in the middle of the screen: a title, a sentence or two, and
 * buttons stacked full width, button "on" filled in (the likely
 * choice, or the current one). ui_dialog_hit says which was tapped
 * (-1 for none), from the same layout.
 */
#define DLG_W 480
#define DLG_BTN_H 72

static int dialog_top(struct app *a, const char *body, int n)
{
	int lines = body ? 4 : 0;
	int h = 90 + lines * 30 + n * (DLG_BTN_H + 14) + 20;
	return (a->h - h) / 2;
}

void ui_dialog(struct app *a, const char *title, const char *body,
	       const char **buttons, int n, int on)
{
	fz_context *ctx = a->ctx;
	int x = (a->w - DLG_W) / 2, top = dialog_top(a, body, n);
	int lines = body ? 4 : 0;
	int h = 90 + lines * 30 + n * (DLG_BTN_H + 14) + 20;

	mr_fill(ctx, a->pix, x, top, DLG_W, h, MR_WHITE);
	mr_outline(ctx, a->pix, x, top, DLG_W, h, 3, MR_BLACK);
	mr_text(ctx, a->pix, x + 28, top + 58, title, MR_BOLD, 28,
		MR_BLACK, DLG_W - 56, MR_LEFT);
	if (body)
		mr_text_wrap(ctx, a->pix, x + 28, top + 100, body,
			     MR_REGULAR, 20, MR_BLACK, DLG_W - 56, lines,
			     30);
	int by = top + 90 + lines * 30;
	for (int i = 0; i < n; i++, by += DLG_BTN_H + 14)
		ui_button(a, x + 28, by, DLG_W - 56, DLG_BTN_H, buttons[i],
			  i == on);
}

int ui_dialog_hit(struct app *a, const char *body, int n, int x, int y)
{
	int dx = (a->w - DLG_W) / 2, top = dialog_top(a, body, n);
	int lines = body ? 4 : 0;
	int by = top + 90 + lines * 30;
	if (x < dx + 28 || x >= dx + DLG_W - 28)
		return -1;
	for (int i = 0; i < n; i++, by += DLG_BTN_H + 14)
		if (y >= by && y < by + DLG_BTN_H)
			return i;
	return -1;
}

static const struct {
	const char *label;
	enum mode mode;
} tabs[] = {
	{ "Home", MODE_HOME },
	{ "Library", MODE_LIBRARY },
	{ "Settings", MODE_SETTINGS },
};
#define N_TABS 3

void ui_tabs(struct app *a)
{
	int top = a->h - TAB_H;
	int tw = a->w / N_TABS;

	mr_fill(a->ctx, a->pix, 0, top, a->w, TAB_H, MR_WHITE);
	mr_fill(a->ctx, a->pix, 0, top, a->w, 2, MR_BLACK);
	for (int i = 0; i < N_TABS; i++) {
		int on = tabs[i].mode == a->mode;
		int cx = i * tw + tw / 2;
		/* the current tab: bold, with a bar along its top */
		if (on)
			mr_fill(a->ctx, a->pix, i * tw + 24, top, tw - 48,
				6, MR_BLACK);
		mr_text(a->ctx, a->pix, cx, top + 52, tabs[i].label,
			on ? MR_BOLD : MR_REGULAR, 24,
			on ? MR_BLACK : MR_GRAY, 0, MR_CENTER);
	}
}

int ui_tab_hit(struct app *a, int x, int y, enum mode *to)
{
	if (y < a->h - TAB_H)
		return 0;
	int i = x / (a->w / N_TABS);
	if (i >= N_TABS)
		i = N_TABS - 1;
	*to = tabs[i].mode;
	return 1;
}

void ui_button(struct app *a, int x, int y, int w, int h,
	       const char *label, int on)
{
	/* on: filled, like a pressed key; off: just an outline */
	if (on)
		mr_fill(a->ctx, a->pix, x, y, w, h, MR_BLACK);
	else
		mr_outline(a->ctx, a->pix, x, y, w, h, 2, MR_BLACK);
	mr_text(a->ctx, a->pix, x + w / 2, y + h / 2 + 8, label,
		on ? MR_BOLD : MR_REGULAR, 22, on ? MR_WHITE : MR_BLACK,
		w - 12, MR_CENTER);
}

void ui_toast(struct app *a)
{
	if (!a->status[0])
		return;
	int y = a->mode == MODE_BOOK ? a->h - TOAST_H
				     : a->h - TAB_H - TOAST_H;
	mr_fill(a->ctx, a->pix, 0, y, a->w, TOAST_H, MR_BLACK);
	mr_text(a->ctx, a->pix, a->w / 2, y + TOAST_H / 2 + 7, a->status,
		MR_BOLD, 20, MR_WHITE, a->w - 2 * MARGIN, MR_CENTER);
}

void ui_go(struct app *a, enum mode to)
{
	/*
	 * Leaving Settings after the text changed: lay the open book
	 * out again with the new settings. open_book restores the
	 * saved place, so the reader lands on the same passage.
	 */
	if (a->mode == MODE_SETTINGS && to != MODE_SETTINGS &&
	    a->relayout) {
		a->relayout = 0;
		mr_book *b = cur_book(a);
		if (b && a->doc)
			open_book(a, b);	/* replaces the old copy */
	}

	/* Home and the library show books most recently read first. */
	if (to == MODE_HOME || to == MODE_LIBRARY) {
		mr_library_sort(&a->lib);
		a->library.dirty = 1;
		a->library.scroll = 0;
	}
	a->mode = to;
	a->down = 0;
}

static int list_height(const struct scroller *s)
{
	return s->pix ? s->pix->h : 0;
}

int scroller_by(struct app *a, struct scroller *s, int d, int view_h)
{
	(void)a;
	int max = list_height(s) - view_h;
	if (max < 0)
		max = 0;
	int old = s->scroll;
	s->scroll += d;
	if (s->scroll > max)
		s->scroll = max;
	if (s->scroll < 0)
		s->scroll = 0;
	return s->scroll != old;
}

void scroller_show(struct app *a, struct scroller *s, int top, int view_h)
{
	fz_context *ctx = a->ctx;
	if (!s->pix)
		return;
	scroller_by(a, s, 0, view_h);	/* the list may have shrunk */

	int stride = fz_pixmap_stride(ctx, a->pix);
	int lstride = fz_pixmap_stride(ctx, s->pix);
	unsigned char *dst = fz_pixmap_samples(ctx, a->pix);
	unsigned char *src = fz_pixmap_samples(ctx, s->pix);
	int total = list_height(s);
	int rows = view_h < total - s->scroll ? view_h : total - s->scroll;
	for (int r = 0; r < rows; r++)
		memcpy(dst + (top + r) * stride,
		       src + (s->scroll + r) * lstride,
		       a->w * fz_pixmap_components(ctx, a->pix));

	/* Scrollbar: where this window sits in the whole list. */
	if (total > view_h) {
		int thumb = view_h * view_h / total;
		if (thumb < 40)
			thumb = 40;
		int ty = top + (view_h - thumb) * s->scroll /
			       (total - view_h);
		mr_fill(ctx, a->pix, a->w - 8, ty, 5, thumb, MR_GRAY);
	}
}
