/*
 * The open book: opening it at the saved place, turning pages, the
 * reading menu (tap the top or bottom edge of a page), and the
 * contents (jump to a chapter).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "app.h"
#include "epub.h"

/* Taps this close to the top or bottom edge open the menu. */
#define MENU_EDGE 110

/* The reading menu's panels and the contents rows, in pixels. */
#define MENU_TOP_H 120
#define MENU_BOTTOM_H 190
#define TOC_ROW_H 76

mr_book *cur_book(struct app *a)
{
	for (int i = 0; i < a->lib.count; i++)
		if (a->lib.items[i].path == a->book_path)
			return &a->lib.items[i];
	return NULL;
}

/* Where the text sits on screen: settings margins, then the footer. */
void text_box(struct app *a, int *x, int *y, int *w, int *h)
{
	int m = setting_margin_px(a);
	*x = m;
	*y = m;
	*w = a->w - 2 * m;
	*h = a->h - FOOTER_H - m - m / 2;
}

/* Record the current place in the book and write the state file. */
static void save_place(struct app *a)
{
	mr_book *b = cur_book(a);
	if (!b || !a->doc)
		return;

	int np = 0;
	fz_var(np);
	fz_try(a->ctx)
		np = fz_count_chapter_pages(a->ctx, a->doc, a->loc.chapter);
	fz_catch(a->ctx)
		np = 0;

	b->chapter = a->loc.chapter;
	b->page = a->loc.page;
	b->chapter_pages = np;
	b->chapters = a->chapters;
	b->last_read = time(NULL);

	/*
	 * Percent: the share of the book before this chapter, plus how
	 * far into this chapter's share the page is. Counting the page
	 * itself as read means the last page reads 100.
	 */
	int ch = a->loc.chapter;
	double in = np > 0 ? (a->loc.page + 1.0) / np : 1.0;
	double done;
	if (a->starts)
		done = a->starts[ch] +
		       (a->starts[ch + 1] - a->starts[ch]) * in;
	else
		done = (ch + in) / a->chapters;
	b->percent = (int)(done * 100.0);
	if (b->percent > 100)
		b->percent = 100;

	mr_library_save(&a->lib);
}

static void drop_toc(struct app *a)
{
	for (int i = 0; i < a->toc_count; i++)
		free(a->toc[i].title);
	free(a->toc);
	a->toc = NULL;
	a->toc_count = 0;
	a->toc_loaded = 0;
	fz_drop_pixmap(a->ctx, a->contents.pix);
	a->contents.pix = NULL;
	a->contents.scroll = 0;
}

void close_book(struct app *a)
{
	drop_toc(a);
	fz_drop_document(a->ctx, a->doc);
	a->doc = NULL;
	a->book_path = NULL;
	free(a->starts);
	a->starts = NULL;
}

/*
 * Open b at its saved place. Only the chapter being read gets laid
 * out (step 02's lesson): never ask for the whole book's page count
 * here, or opening a long book stalls. 0 on success.
 */
int open_book(struct app *a, mr_book *b)
{
	fz_context *ctx = a->ctx;
	fz_document *doc = NULL;
	fz_location loc = fz_make_location(0, 0);
	int chapters = 0;
	int tx, ty, tw, th;
	fz_var(doc);
	fz_var(loc);
	fz_var(chapters);

	text_box(a, &tx, &ty, &tw, &th);
	fz_try(ctx) {
		doc = fz_open_document(ctx, b->path);
		fz_layout_document(ctx, doc, tw * 72.0f / SCREEN_DPI,
				   th * 72.0f / SCREEN_DPI,
				   setting_font_pt(a));
		chapters = fz_count_chapters(ctx, doc);
		if (chapters <= 0)
			fz_throw(ctx, FZ_ERROR_FORMAT, "no chapters");

		if (b->chapter >= 0 && b->chapter < chapters) {
			int np = fz_count_chapter_pages(ctx, doc,
							b->chapter);
			int pg = b->page;
			/*
			 * The chapter re-flowed to a different length
			 * since the save (new text size): keep the same
			 * fraction of the way through it.
			 */
			if (b->chapter_pages > 0 && np != b->chapter_pages)
				pg = (int)((long long)b->page * np /
					   b->chapter_pages);
			if (pg >= np)
				pg = np - 1;
			if (pg < 0)
				pg = 0;
			loc = fz_make_location(b->chapter, pg);
		}
	}
	fz_catch(ctx) {
		fprintf(stderr, "%s: %s\n", b->path,
			fz_caught_message(ctx));
		fz_drop_document(ctx, doc);
		snprintf(a->status, sizeof a->status,
			 "Couldn't open %s", b->title);
		return -1;
	}

	close_book(a);
	a->doc = doc;
	a->loc = loc;
	a->chapters = chapters;
	a->book_path = b->path;
	/* reads each chapter's file once: 16 ms worst case on the Mac */
	a->starts = mr_chapter_starts(ctx, b->path, chapters);
	a->mode = MODE_BOOK;
	a->library.dirty = 1;	/* the open-book marker moved */
	save_place(a);
	return 0;
}

/* Move one page; dir is +1 or -1. Returns 1 if the page changed. */
static int turn(struct app *a, int dir)
{
	fz_location next = a->loc;
	fz_var(next);
	fz_try(a->ctx) {
		if (dir > 0)
			next = fz_next_page(a->ctx, a->doc, a->loc);
		else
			next = fz_previous_page(a->ctx, a->doc, a->loc);
	}
	fz_catch(a->ctx)
		next = a->loc;

	/* At either end of the book, MuPDF hands back the same place. */
	if (next.chapter == a->loc.chapter && next.page == a->loc.page)
		return 0;
	a->loc = next;
	save_place(a);
	return 1;
}

void draw_book(struct app *a)
{
	fz_context *ctx = a->ctx;
	fz_page *page = NULL;
	fz_device *dev = NULL;
	int tx, ty, tw, th;
	fz_var(page);
	fz_var(dev);

	text_box(a, &tx, &ty, &tw, &th);
	fz_clear_pixmap_with_value(ctx, a->pix, MR_WHITE);
	fz_try(ctx) {
		page = fz_load_chapter_page(ctx, a->doc, a->loc.chapter,
					    a->loc.page);
		/* points to pixels, then over by the margins */
		fz_matrix ctm = fz_concat(fz_scale(SCREEN_DPI / 72.0f,
						   SCREEN_DPI / 72.0f),
					  fz_translate(tx, ty));
		dev = fz_new_draw_device(ctx, fz_identity, a->pix);
		fz_run_page(ctx, page, dev, ctm, NULL);
		fz_close_device(ctx, dev);
	}
	fz_always(ctx) {
		fz_drop_device(ctx, dev);
		fz_drop_page(ctx, page);
	}
	fz_catch(ctx) {
		fprintf(stderr, "page %d.%d: %s\n", a->loc.chapter,
			a->loc.page, fz_caught_message(ctx));
		snprintf(a->status, sizeof a->status,
			 "This page couldn't be drawn");
	}

	/*
	 * Footer: the title on the left, the time in the middle, how far
	 * along on the right.
	 */
	mr_book *b = cur_book(a);
	int y = a->h - FOOTER_H + 26;
	int pct = b ? mr_book_percent(b) : -1;
	char buf[16];
	if (pct >= 0) {
		snprintf(buf, sizeof buf, "%d%%", pct);
		mr_text(ctx, a->pix, a->w - MARGIN, y, buf, MR_REGULAR, 16,
			MR_GRAY, 0, MR_RIGHT);
	}
	time_t now = app_time(a);
	ui_time(a, now, buf, sizeof buf);
	mr_text(ctx, a->pix, a->w / 2, y, buf, MR_REGULAR, 16, MR_GRAY, 0,
		MR_CENTER);
	struct tm tm;
	localtime_r(&now, &tm);
	a->shown_minute = tm.tm_hour * 60 + tm.tm_min;
	if (b)
		mr_text(ctx, a->pix, MARGIN, y, b->title, MR_REGULAR, 16,
			MR_GRAY, a->w * 0.36f, MR_LEFT);
}

int on_book(struct app *a, const struct action *act)
{
	switch (act->type) {
	case ACT_NEXT:
		return turn(a, +1);
	case ACT_PREV:
		return turn(a, -1);
	case ACT_SWIPE:
		return turn(a, act->d);
	case ACT_TAP:
		if (act->y < MENU_EDGE || act->y > a->h - MENU_EDGE) {
			a->mode = MODE_MENU;
			return 1;
		}
		return turn(a, act->x < a->w / 3 ? -1 : +1);
	case ACT_HOLD:
		return lookup_word(a, act->x, act->y);
	default:
		return 0;
	}
}

/* ---- the reading menu ---- */

/* The three buttons along the bottom panel. */
static void menu_button_box(struct app *a, int i, int *x, int *y, int *w,
			    int *h)
{
	int gap = 16;
	*w = (a->w - 2 * MARGIN - 2 * gap) / 3;
	*h = 72;
	*x = MARGIN + i * (*w + gap);
	*y = a->h - MARGIN - *h;
}

static const char *menu_labels[] = { "Home", "Contents", "Settings" };

void draw_menu(struct app *a)
{
	fz_context *ctx = a->ctx;
	mr_book *b = cur_book(a);

	draw_book(a);

	/* Top panel: what you're reading. */
	mr_fill(ctx, a->pix, 0, 0, a->w, MENU_TOP_H, MR_WHITE);
	mr_fill(ctx, a->pix, 0, MENU_TOP_H - 2, a->w, 2, MR_BLACK);
	ui_status(a, a->w - MARGIN, 50, MR_GRAY);
	if (b) {
		mr_text(ctx, a->pix, MARGIN, 52, b->title, MR_BOLD, 28,
			MR_BLACK, a->w - 2 * MARGIN - 200, MR_LEFT);
		mr_text(ctx, a->pix, MARGIN, 92, b->author, MR_REGULAR, 20,
			MR_GRAY, a->w - 2 * MARGIN, MR_LEFT);
	}

	/* Bottom panel: how far along, and where to go. */
	int top = a->h - MENU_BOTTOM_H;
	mr_fill(ctx, a->pix, 0, top, a->w, MENU_BOTTOM_H, MR_WHITE);
	mr_fill(ctx, a->pix, 0, top, a->w, 2, MR_BLACK);

	int pct = b ? mr_book_percent(b) : -1;
	if (pct < 0)
		pct = 0;
	int bar_w = a->w - 2 * MARGIN - 80;
	mr_fill(ctx, a->pix, MARGIN, top + 34, bar_w, 8, MR_LIGHT);
	mr_fill(ctx, a->pix, MARGIN, top + 34, bar_w * pct / 100, 8,
		MR_BLACK);
	char buf[16];
	snprintf(buf, sizeof buf, "%d%%", pct);
	mr_text(ctx, a->pix, a->w - MARGIN, top + 46, buf, MR_BOLD, 22,
		MR_BLACK, 0, MR_RIGHT);

	for (int i = 0; i < 3; i++) {
		int x, y, w, h;
		menu_button_box(a, i, &x, &y, &w, &h);
		ui_button(a, x, y, w, h, menu_labels[i], 0);
	}
}

static void open_contents(struct app *a);

int on_menu(struct app *a, const struct action *act)
{
	if (act->type == ACT_NEXT || act->type == ACT_PREV ||
	    act->type == ACT_SWIPE) {
		a->mode = MODE_BOOK;
		return on_book(a, act);
	}
	if (act->type != ACT_TAP)
		return 0;

	for (int i = 0; i < 3; i++) {
		int x, y, w, h;
		menu_button_box(a, i, &x, &y, &w, &h);
		if (act->x >= x && act->x < x + w && act->y >= y &&
		    act->y < y + h) {
			if (i == 0)
				ui_go(a, MODE_HOME);
			else if (i == 1)
				open_contents(a);
			else
				ui_go(a, MODE_SETTINGS);
			return 1;
		}
	}
	/* a tap on the page itself puts the menu away */
	if (act->y > MENU_TOP_H && act->y < a->h - MENU_BOTTOM_H) {
		a->mode = MODE_BOOK;
		return 1;
	}
	return 0;
}

/* ---- contents ---- */

/* Where a link goes. The fz_try is alone here; see try_render_cover. */
static fz_location try_resolve(struct app *a, const char *uri)
{
	fz_location loc = fz_make_location(-1, -1);
	fz_var(loc);
	fz_try(a->ctx)
		loc = fz_resolve_link(a->ctx, a->doc, uri, NULL, NULL);
	fz_catch(a->ctx)
		loc = fz_make_location(-1, -1);
	return loc;
}

static void add_entries(struct app *a, fz_outline *o, int depth, int *cap)
{
	for (; o; o = o->next) {
		if (o->title && o->title[0]) {
			if (a->toc_count == *cap) {
				*cap = *cap ? *cap * 2 : 64;
				a->toc = realloc(a->toc,
						 *cap * sizeof *a->toc);
				if (!a->toc) {
					fprintf(stderr, "out of memory\n");
					exit(1);
				}
			}
			struct toc_entry *e = &a->toc[a->toc_count++];
			e->title = strdup(o->title);
			e->loc = o->page;
			/* not worked out yet: ask where the link goes */
			if (e->loc.chapter < 0 && o->uri)
				e->loc = try_resolve(a, o->uri);
			e->depth = depth;
		}
		add_entries(a, o->down, depth + 1, cap);
	}
}

/* The entry for the chapter being read: the last one at or before it. */
static int current_entry(struct app *a)
{
	int best = -1;
	for (int i = 0; i < a->toc_count; i++) {
		fz_location l = a->toc[i].loc;
		if (l.chapter < 0)
			continue;
		if (l.chapter < a->loc.chapter ||
		    (l.chapter == a->loc.chapter && l.page <= a->loc.page))
			best = i;
	}
	return best;
}

static void build_contents(struct app *a)
{
	fz_context *ctx = a->ctx;
	int n = a->toc_count ? a->toc_count : 1;

	fz_drop_pixmap(ctx, a->contents.pix);
	a->contents.pix = NULL;
	a->contents.pix = fz_new_pixmap(ctx, fz_device_gray(ctx), a->w,
					n * TOC_ROW_H + 40, NULL, 0);
	fz_pixmap *pix = a->contents.pix;
	fz_clear_pixmap_with_value(ctx, pix, MR_WHITE);

	if (a->toc_count == 0)
		mr_text(ctx, pix, a->w / 2, 48, "This book has no contents",
			MR_REGULAR, 22, MR_GRAY, 0, MR_CENTER);

	int cur = current_entry(a);
	for (int i = 0; i < a->toc_count; i++) {
		struct toc_entry *e = &a->toc[i];
		int y = i * TOC_ROW_H;
		int indent = MARGIN + (e->depth > 3 ? 3 : e->depth) * 28;
		if (i == cur)
			mr_fill(ctx, pix, 0, y + 14, 8, TOC_ROW_H - 28,
				MR_BLACK);
		mr_text(ctx, pix, indent, y + 47, e->title,
			e->depth == 0 ? MR_BOLD : MR_REGULAR, 22,
			e->loc.chapter < 0 ? MR_LIGHT : MR_BLACK,
			a->w - indent - MARGIN, MR_LEFT);
		mr_fill(ctx, pix, MARGIN, y + TOC_ROW_H - 1,
			a->w - 2 * MARGIN, 1, MR_LIGHT);
	}
	a->contents.dirty = 0;
}

static void open_contents(struct app *a)
{
	if (!a->toc_loaded) {
		fz_outline *outline = NULL;
		int cap = 0;
		fz_var(outline);
		fz_try(a->ctx) {
			outline = fz_load_outline(a->ctx, a->doc);
			add_entries(a, outline, 0, &cap);
		}
		fz_always(a->ctx)
			fz_drop_outline(a->ctx, outline);
		fz_catch(a->ctx)
			fprintf(stderr, "contents: %s\n",
				fz_caught_message(a->ctx));
		a->toc_loaded = 1;
	}
	a->contents.dirty = 1;
	a->mode = MODE_CONTENTS;

	/* start with the current chapter in view, a row from the top */
	int cur = current_entry(a);
	a->contents.scroll = cur > 0 ? (cur - 1) * TOC_ROW_H : 0;
}

/* "Done" in the header: back to the page. */
static int done_hit(struct app *a, int x, int y)
{
	return y < HEAD_H && x > a->w - 160;
}

void draw_contents(struct app *a)
{
	fz_clear_pixmap_with_value(a->ctx, a->pix, MR_WHITE);
	if (a->contents.dirty || !a->contents.pix)
		build_contents(a);
	scroller_show(a, &a->contents, HEAD_H, a->h - HEAD_H);
	ui_header(a, "Contents");
	ui_button(a, a->w - MARGIN - 110, 22, 110, 52, "Done", 0);
}

int on_contents(struct app *a, const struct action *act)
{
	int view = a->h - HEAD_H;
	switch (act->type) {
	case ACT_DRAG:
	case ACT_SCROLL:
		return scroller_by(a, &a->contents, act->d, view);
	case ACT_NEXT:
		return scroller_by(a, &a->contents, view - TOC_ROW_H, view);
	case ACT_PREV:
		return scroller_by(a, &a->contents, -(view - TOC_ROW_H),
				   view);
	case ACT_TAP:
		break;
	default:
		return 0;
	}

	if (done_hit(a, act->x, act->y)) {
		a->mode = MODE_BOOK;
		return 1;
	}
	if (act->y < HEAD_H)
		return 0;
	int i = (act->y - HEAD_H + a->contents.scroll) / TOC_ROW_H;
	if (i >= a->toc_count || a->toc[i].loc.chapter < 0)
		return 0;

	fz_location to = a->toc[i].loc;
	fz_try(a->ctx)
		to = fz_clamp_location(a->ctx, a->doc, to);
	fz_catch(a->ctx)
		return 0;
	a->loc = to;
	save_place(a);
	a->contents.dirty = 1;	/* the current-chapter bar moves */
	a->mode = MODE_BOOK;
	return 1;
}
