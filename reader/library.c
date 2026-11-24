/*
 * The library: every book, one scrolling list. A bar under the title
 * shows how many and how they're sorted; its Sort button picks the
 * order (last read, title, author, date added, date published). Drag
 * (or the wheel) to scroll, tap to open.
 */
#include <stdio.h>
#include <string.h>
#include "app.h"

#define BAR_H 64	/* the count and the Sort button */
#define LIST_TOP (HEAD_H + BAR_H)
#define ROW_H 100
#define END_H 40	/* room under the last book */
#define STATUS_W 90	/* the right end of a row: "42%" or "Done" */
#define SORT_W 220

static const char *sort_names[MR_SORT_COUNT] = {
	"Recently read", "Title", "Author", "Date added", "Date published",
};

static int view_h(const struct app *a)
{
	return a->h - LIST_TOP - TAB_H;
}

/* "Bret Easton Ellis, 1991" or ", added Nov 24, 2026", by the sort. */
static void subtitle(struct app *a, const mr_book *b, char *buf,
		     size_t n)
{
	const char *who = b->author ? b->author : "";
	const char *sep = who[0] ? ", " : "";
	if (a->lib.set.sort == MR_SORT_PUBLISHED && b->published &&
	    b->published[0]) {
		snprintf(buf, n, "%s%s%.4s", who, sep, b->published);
	} else if (a->lib.set.sort == MR_SORT_ADDED) {
		char date[32];
		time_t t = (time_t)b->mtime;
		struct tm tm;
		localtime_r(&t, &tm);
		strftime(date, sizeof date, "%b %e, %Y", &tm);
		snprintf(buf, n, "%s%sadded %s", who, sep, date);
	} else {
		snprintf(buf, n, "%s", who);
	}
}

/* Draw every row of the list into its tall picture. */
static void build_list(struct app *a)
{
	fz_context *ctx = a->ctx;
	char buf[300];
	int n = a->lib.seen_count;

	fz_drop_pixmap(ctx, a->library.pix);
	a->library.pix = NULL;
	a->library.pix = mr_new_pixmap(ctx, a->w, n * ROW_H + END_H);
	fz_pixmap *pix = a->library.pix;
	fz_clear_pixmap_with_value(ctx, pix, MR_WHITE);

	if (n == 0)
		mr_text(ctx, pix, a->w / 2, 30, "No books found",
			MR_REGULAR, 22, MR_GRAY, 0, MR_CENTER);

	float text_w = a->w - 2 * MARGIN - STATUS_W;
	for (int i = 0; i < n; i++) {
		mr_book *b = &a->lib.items[i];
		int y = i * ROW_H;

		/* a bar at the left edge marks the open book */
		if (b->path == a->book_path)
			mr_fill(ctx, pix, 0, y + 16, 8, ROW_H - 32,
				MR_BLACK);

		mr_text(ctx, pix, MARGIN, y + 44, b->title, MR_BOLD, 26,
			MR_BLACK, text_w, MR_LEFT);
		subtitle(a, b, buf, sizeof buf);
		mr_text(ctx, pix, MARGIN, y + 78, buf, MR_REGULAR, 20,
			MR_GRAY, text_w, MR_LEFT);

		int pct = mr_book_percent(b);
		if (pct >= 100)
			snprintf(buf, sizeof buf, "Done");
		else if (pct >= 0)
			snprintf(buf, sizeof buf, "%d%%", pct);
		else
			buf[0] = 0;
		if (buf[0])
			mr_text(ctx, pix, a->w - MARGIN, y + 44, buf,
				MR_REGULAR, 22, MR_GRAY, 0, MR_RIGHT);

		mr_fill(ctx, pix, MARGIN, y + ROW_H - 1,
			a->w - 2 * MARGIN, 1, MR_LIGHT);
	}
	a->library.dirty = 0;
}

static void sort_button(struct app *a, int *x, int *y, int *w, int *h)
{
	*w = SORT_W;
	*h = 48;
	*x = a->w - MARGIN - SORT_W;
	*y = HEAD_H + (BAR_H - 48) / 2;
}

void draw_library(struct app *a)
{
	char buf[64];

	fz_clear_pixmap_with_value(a->ctx, a->pix, MR_WHITE);
	if (a->library.dirty || !a->library.pix)
		build_list(a);
	scroller_show(a, &a->library, LIST_TOP, view_h(a));

	ui_header(a, "Library");

	/* the bar: how many, and how they're sorted */
	mr_fill(a->ctx, a->pix, 0, HEAD_H, a->w, BAR_H, MR_WHITE);
	snprintf(buf, sizeof buf, "%d books", a->lib.seen_count);
	mr_text(a->ctx, a->pix, MARGIN, HEAD_H + BAR_H / 2 + 8, buf,
		MR_REGULAR, 20, MR_GRAY, 0, MR_LEFT);
	int x, y, w, h;
	sort_button(a, &x, &y, &w, &h);
	snprintf(buf, sizeof buf, "Sort: %s",
		 sort_names[a->lib.set.sort % MR_SORT_COUNT]);
	ui_button(a, x, y, w, h, buf, 0);
	mr_fill(a->ctx, a->pix, 0, LIST_TOP - 1, a->w, 1, MR_LIGHT);

	ui_tabs(a);
	if (a->sort_open)
		ui_dialog(a, "Sort by", NULL, sort_names, MR_SORT_COUNT,
			  a->lib.set.sort);
}

static int on_sort_box(struct app *a, const struct action *act)
{
	if (act->type != ACT_TAP)
		return 0;
	int i = ui_dialog_hit(a, NULL, MR_SORT_COUNT, act->x, act->y);
	a->sort_open = 0;
	if (i >= 0 && i != a->lib.set.sort) {
		a->lib.set.sort = i;
		mr_library_sort(&a->lib);
		mr_library_save(&a->lib);
		a->library.dirty = 1;
		a->library.scroll = 0;
	}
	return 1;	/* a tap outside just closes it */
}

int on_library(struct app *a, const struct action *act)
{
	int screenful = view_h(a) - ROW_H;

	if (a->sort_open)
		return on_sort_box(a, act);

	switch (act->type) {
	case ACT_DRAG:
	case ACT_SCROLL:
		return scroller_by(a, &a->library, act->d, view_h(a));
	case ACT_NEXT:
		return scroller_by(a, &a->library, screenful, view_h(a));
	case ACT_PREV:
		return scroller_by(a, &a->library, -screenful, view_h(a));
	case ACT_TAP:
		break;
	default:
		return 0;
	}

	enum mode to;
	if (ui_tab_hit(a, act->x, act->y, &to)) {
		if (to == MODE_LIBRARY)	/* tapping it again: to the top */
			return scroller_by(a, &a->library,
					   -a->library.scroll, view_h(a));
		ui_go(a, to);
		return 1;
	}
	if (act->y < LIST_TOP) {
		int x, y, w, h;
		sort_button(a, &x, &y, &w, &h);
		if (act->x >= x && act->x < x + w && act->y >= y &&
		    act->y < y + h) {
			a->sort_open = 1;
			return 1;
		}
		return 0;
	}

	int i = (act->y - LIST_TOP + a->library.scroll) / ROW_H;
	if (i >= a->lib.seen_count)
		return 0;
	mr_book *b = &a->lib.items[i];
	if (b->path == a->book_path && a->doc) {
		a->mode = MODE_BOOK;	/* already open: no reload */
		return 1;
	}
	open_book(a, b);	/* on failure, status says why */
	return 1;
}
