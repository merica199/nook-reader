/*
 * Home: where the n button always goes. The book you're reading, big,
 * with its cover and how far along you are; below it the books you
 * read before that; along the bottom the tabs.
 *
 * Covers are drawn after dark mode remaps the frame (draw_home_covers,
 * called from reader.c), so they never turn into photo negatives.
 */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "epub.h"

/* The "continue reading" card. */
#define CARD_TOP (HEAD_H + 64)
#define BIG_W 180
#define BIG_H 270

/* The "recently read" row. */
#define RECENT_LABEL_Y (CARD_TOP + BIG_H + 72)
#define RECENT_TOP (RECENT_LABEL_Y + 20)
#define SMALL_W 150
#define SMALL_H 225
#define N_RECENT 3

/* ---- covers ---- */

/* Shrink an image to fit w x h, keeping its shape. */
static fz_pixmap *render_cover(fz_context *ctx, fz_image *img, int w,
			       int h)
{
	float s = (float)w / img->w;
	if (img->h * s > h)
		s = (float)h / img->h;
	int dw = (int)(img->w * s), dh = (int)(img->h * s);
	if (dw < 1 || dh < 1)
		return NULL;

	fz_pixmap *pix = fz_new_pixmap(ctx, fz_device_gray(ctx), dw, dh,
				       NULL, 0);
	fz_device *dev = NULL;
	fz_var(dev);
	fz_try(ctx) {
		fz_clear_pixmap_with_value(ctx, pix, MR_WHITE);
		dev = fz_new_draw_device(ctx, fz_identity, pix);
		/* an image fills the unit square; stretch it to dw x dh */
		fz_fill_image(ctx, dev, img, fz_make_matrix(dw, 0, 0, dh,
							    0, 0),
			      1.0f, fz_default_color_params);
		fz_close_device(ctx, dev);
	}
	fz_always(ctx)
		fz_drop_device(ctx, dev);
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pix);
		pix = NULL;
	}
	return pix;
}

/*
 * The fz_try lives in its own small function on purpose: after a
 * longjmp, a local changed since the setjmp is undefined (C11
 * 7.13.2.1), and GCC's -Wclobbered flags every register-held local
 * near a setjmp. With nothing else here, there's nothing to clobber.
 */
static fz_pixmap *try_render_cover(fz_context *ctx, fz_image *img,
				   int w, int h)
{
	fz_pixmap *pix = NULL;
	fz_var(pix);
	fz_try(ctx)
		pix = render_cover(ctx, img, w, h);
	fz_catch(ctx)
		pix = NULL;
	return pix;
}

/*
 * The cover for b at w x h, from the cache or freshly made. A small
 * cache: home shows at most four covers.
 */
static struct cover *get_cover(struct app *a, mr_book *b, int w, int h)
{
	for (int i = 0; i < MAX_COVERS; i++) {
		struct cover *c = &a->covers[i];
		if (c->path == b->path && c->w == w && c->h == h)
			return c;
	}

	struct cover *c = &a->covers[a->next_cover];
	a->next_cover = (a->next_cover + 1) % MAX_COVERS;
	fz_drop_pixmap(a->ctx, c->pix);
	c->pix = NULL;
	c->path = b->path;
	c->w = w;
	c->h = h;

	fz_image *img = mr_epub_cover(a->ctx, b->path);
	if (img) {
		c->pix = try_render_cover(a->ctx, img, w, h);
		fz_drop_image(a->ctx, img);
	}
	return c;
}

void drop_covers(struct app *a)
{
	for (int i = 0; i < MAX_COVERS; i++) {
		fz_drop_pixmap(a->ctx, a->covers[i].pix);
		memset(&a->covers[i], 0, sizeof a->covers[i]);
	}
}

/* Where a cover's picture sits in its box: centered, bottom-aligned. */
static void cover_spot(struct cover *c, int x, int y, int *cx, int *cy)
{
	*cx = x + (c->w - c->pix->w) / 2;
	*cy = y + (c->h - c->pix->h);
}

/*
 * The frame around a cover, or a stand-in when there's no picture:
 * a plain card with the title on it.
 */
static void cover_frame(struct app *a, mr_book *b, int x, int y, int w,
			int h)
{
	struct cover *c = get_cover(a, b, w, h);
	if (c->pix) {
		int cx, cy;
		cover_spot(c, x, y, &cx, &cy);
		mr_outline(a->ctx, a->pix, cx - 1, cy - 1, c->pix->w + 2,
			   c->pix->h + 2, 1, MR_LIGHT);
		return;
	}
	mr_outline(a->ctx, a->pix, x, y, w, h, 2, MR_BLACK);
	mr_text_wrap(a->ctx, a->pix, x + 12, y + 40, b->title, MR_BOLD,
		     w > 160 ? 22 : 18, MR_BLACK, w - 24, 5,
		     w > 160 ? 28 : 23);
}

static void blit(struct app *a, fz_pixmap *src, int x, int y)
{
	int stride = fz_pixmap_stride(a->ctx, a->pix);
	int sstride = fz_pixmap_stride(a->ctx, src);
	unsigned char *d = fz_pixmap_samples(a->ctx, a->pix);
	unsigned char *s = fz_pixmap_samples(a->ctx, src);
	for (int r = 0; r < src->h && y + r < a->h; r++)
		memcpy(d + (y + r) * stride + x, s + r * sstride,
		       src->w);
}

/*
 * For other screens (the lock screen): a cover's frame or stand-in,
 * drawn with the rest of the screen, and its picture, drawn after
 * dark mode's remap.
 */
void cover_box(struct app *a, mr_book *b, int x, int y, int w, int h)
{
	cover_frame(a, b, x, y, w, h);
}

void cover_picture(struct app *a, mr_book *b, int x, int y, int w,
		   int h)
{
	struct cover *c = get_cover(a, b, w, h);
	if (c->pix) {
		int cx, cy;
		cover_spot(c, x, y, &cx, &cy);
		blit(a, c->pix, cx, cy);
	}
}

/* ---- the screen ---- */

/* Books read, most recent first; items are sorted that way. */
static mr_book *read_book(struct app *a, int n)
{
	if (n < a->lib.seen_count && a->lib.items[n].last_read > 0)
		return &a->lib.items[n];
	return NULL;
}

static int small_x(struct app *a, int i)
{
	int gap = (a->w - 2 * MARGIN - N_RECENT * SMALL_W) /
		  (N_RECENT - 1);
	return MARGIN + i * (SMALL_W + gap);
}

/* The button on the welcome screen, before any book is read. */
static void welcome_button(struct app *a, int *x, int *y, int *w, int *h)
{
	*w = 280;
	*h = 72;
	*x = (a->w - *w) / 2;
	*y = a->h / 2;
}

void draw_home(struct app *a)
{
	fz_context *ctx = a->ctx;
	fz_pixmap *pix = a->pix;
	char buf[64];

	fz_clear_pixmap_with_value(ctx, pix, MR_WHITE);
	ui_header(a, "Home");

	mr_book *b = read_book(a, 0);
	if (!b) {
		mr_text(ctx, pix, a->w / 2, a->h / 2 - 110, "Nothing read yet",
			MR_BOLD, 32, MR_BLACK, 0, MR_CENTER);
		mr_text(ctx, pix, a->w / 2, a->h / 2 - 60,
			"Pick a book from your library.", MR_REGULAR, 22,
			MR_GRAY, 0, MR_CENTER);
		int x, y, w, h;
		welcome_button(a, &x, &y, &w, &h);
		ui_button(a, x, y, w, h, "Open the library", 0);
		ui_tabs(a);
		return;
	}

	/* Continue reading. */
	mr_text(ctx, pix, MARGIN, HEAD_H + 44, "Continue reading",
		MR_REGULAR, 20, MR_GRAY, 0, MR_LEFT);
	cover_frame(a, b, MARGIN, CARD_TOP, BIG_W, BIG_H);

	int tx = MARGIN + BIG_W + 24, tw = a->w - tx - MARGIN;
	int lines = mr_text_wrap(ctx, pix, tx, CARD_TOP + 30, b->title,
				 MR_BOLD, 28, MR_BLACK, tw, 4, 34);
	mr_text(ctx, pix, tx, CARD_TOP + 30 + lines * 34 + 4, b->author,
		MR_REGULAR, 20, MR_GRAY, tw, MR_LEFT);

	int pct = mr_book_percent(b);
	if (pct < 0)
		pct = 0;
	int bar_y = CARD_TOP + BIG_H - 46;
	mr_fill(ctx, pix, tx, bar_y, tw, 8, MR_LIGHT);
	mr_fill(ctx, pix, tx, bar_y, tw * pct / 100, 8, MR_BLACK);
	if (pct >= 100)
		snprintf(buf, sizeof buf, "Finished");
	else
		snprintf(buf, sizeof buf, "%d%% read", pct);
	mr_text(ctx, pix, tx, bar_y + 38, buf, MR_REGULAR, 20, MR_GRAY, 0,
		MR_LEFT);

	/* Recently read: the books before this one. */
	mr_text(ctx, pix, MARGIN, RECENT_LABEL_Y, "Recently read",
		MR_REGULAR, 20, MR_GRAY, 0, MR_LEFT);
	if (!read_book(a, 1))
		mr_text(ctx, pix, MARGIN, RECENT_TOP + 40,
			"Other books you open show up here.", MR_REGULAR,
			20, MR_LIGHT, 0, MR_LEFT);
	for (int i = 0; i < N_RECENT; i++) {
		mr_book *r = read_book(a, i + 1);
		if (!r)
			break;
		int x = small_x(a, i);
		cover_frame(a, r, x, RECENT_TOP, SMALL_W, SMALL_H);
		mr_text_wrap(ctx, pix, x, RECENT_TOP + SMALL_H + 28,
			     r->title, MR_REGULAR, 18, MR_BLACK, SMALL_W,
			     2, 22);
	}

	snprintf(buf, sizeof buf, "%d books in your library",
		 a->lib.seen_count);
	mr_text(ctx, pix, a->w / 2, a->h - TAB_H - 30, buf, MR_REGULAR, 18,
		MR_LIGHT, 0, MR_CENTER);
	ui_tabs(a);
}

void draw_home_covers(struct app *a)
{
	mr_book *b = read_book(a, 0);
	if (!b)
		return;
	struct cover *c = get_cover(a, b, BIG_W, BIG_H);
	int cx, cy;
	if (c->pix) {
		cover_spot(c, MARGIN, CARD_TOP, &cx, &cy);
		blit(a, c->pix, cx, cy);
	}
	for (int i = 0; i < N_RECENT; i++) {
		mr_book *r = read_book(a, i + 1);
		if (!r)
			break;
		c = get_cover(a, r, SMALL_W, SMALL_H);
		if (c->pix) {
			cover_spot(c, small_x(a, i), RECENT_TOP, &cx, &cy);
			blit(a, c->pix, cx, cy);
		}
	}
}

static int open_or_resume(struct app *a, mr_book *b)
{
	if (b->path == a->book_path && a->doc) {
		a->mode = MODE_BOOK;
		return 1;
	}
	open_book(a, b);	/* on failure, status says why */
	return 1;
}

int on_home(struct app *a, const struct action *act)
{
	if (act->type != ACT_TAP)
		return 0;
	int x = act->x, y = act->y;

	enum mode to;
	if (ui_tab_hit(a, x, y, &to)) {
		if (to == MODE_HOME)
			return 0;
		ui_go(a, to);
		return 1;
	}

	mr_book *b = read_book(a, 0);
	if (!b) {
		int bx, by, bw, bh;
		welcome_button(a, &bx, &by, &bw, &bh);
		if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
			ui_go(a, MODE_LIBRARY);
			return 1;
		}
		return 0;
	}

	/* anywhere on the card resumes the book */
	if (y >= CARD_TOP && y < CARD_TOP + BIG_H)
		return open_or_resume(a, b);

	if (y >= RECENT_TOP && y < RECENT_TOP + SMALL_H + 60) {
		for (int i = 0; i < N_RECENT; i++) {
			mr_book *r = read_book(a, i + 1);
			int sx = small_x(a, i);
			if (r && x >= sx && x < sx + SMALL_W)
				return open_or_resume(a, r);
		}
	}
	return 0;
}
