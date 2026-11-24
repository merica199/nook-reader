/*
 * The library: every book, one scrolling list, most recently read
 * first and then by title. Drag (or the wheel) to scroll, tap to
 * open.
 */
#include <stdio.h>
#include "app.h"

#define ROW_H 100
#define END_H 40	/* room under the last book */
#define STATUS_W 90	/* the right end of a row: "42%" or "Done" */

static int view_h(const struct app *a)
{
	return a->h - HEAD_H - TAB_H;
}

/* Draw every row of the list into its tall picture. */
static void build_list(struct app *a)
{
	fz_context *ctx = a->ctx;
	char buf[32];
	int n = a->lib.seen_count;

	fz_drop_pixmap(ctx, a->library.pix);
	a->library.pix = NULL;
	a->library.pix = fz_new_pixmap(ctx, fz_device_gray(ctx), a->w,
				       n * ROW_H + END_H, NULL, 0);
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
		mr_text(ctx, pix, MARGIN, y + 78, b->author, MR_REGULAR,
			20, MR_GRAY, text_w, MR_LEFT);

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

void draw_library(struct app *a)
{
	char buf[32];

	fz_clear_pixmap_with_value(a->ctx, a->pix, MR_WHITE);
	if (a->library.dirty || !a->library.pix)
		build_list(a);
	scroller_show(a, &a->library, HEAD_H, view_h(a));

	ui_header(a, "Library");
	snprintf(buf, sizeof buf, "%d books", a->lib.seen_count);
	mr_text(a->ctx, a->pix, a->w - MARGIN, 62, buf, MR_REGULAR, 20,
		MR_GRAY, 0, MR_RIGHT);
	ui_tabs(a);
}

int on_library(struct app *a, const struct action *act)
{
	int screenful = view_h(a) - ROW_H;

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
	if (act->y < HEAD_H)
		return 0;

	int i = (act->y - HEAD_H + a->library.scroll) / ROW_H;
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
