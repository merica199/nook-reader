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

/* The "continue reading" card: one row, a small cover and the text. */
#define CARD_TOP (HEAD_H + 56)
#define BIG_W 80
#define BIG_H 120

/* "Recently read": a grid of covers, N_COLS across, N_ROWS down. */
#define RECENT_LABEL_Y (CARD_TOP + BIG_H + 46)
#define RECENT_TOP (RECENT_LABEL_Y + 18)
#define N_COLS 4
#define N_ROWS 2
#define N_RECENT (N_COLS * N_ROWS)
#define GAP 16
#define SMALL_W ((MR_NOOK_W - 2 * MARGIN - (N_COLS - 1) * GAP) / N_COLS)
#define SMALL_H (SMALL_W * 3 / 2)
#define CELL_H (SMALL_H + 56)	/* cover, then two lines of title */

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

	fz_pixmap *pix = mr_new_pixmap(ctx, dw, dh);
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
	/* the stand-in's title, sized to the card */
	int size = w > 160 ? 22 : w > 100 ? 16 : 12;
	int pad = w > 100 ? 12 : 6;
	mr_outline(a->ctx, a->pix, x, y, w, h, 2, MR_BLACK);
	mr_text_wrap(a->ctx, a->pix, x + pad, y + pad + size, b->title,
		     MR_BOLD, size, MR_BLACK, w - 2 * pad, 5,
		     size * 5 / 4);
}

static void blit(struct app *a, fz_pixmap *src, int x, int y)
{
	int stride = fz_pixmap_stride(a->ctx, a->pix);
	int sstride = fz_pixmap_stride(a->ctx, src);
	unsigned char *d = fz_pixmap_samples(a->ctx, a->pix);
	unsigned char *s = fz_pixmap_samples(a->ctx, src);
	for (int r = 0; r < src->h && y + r < a->h; r++)
		memcpy(d + (y + r) * stride + x * src->n,
		       s + r * sstride, src->w * src->n);
}

/* ---- the screen ---- */

/* Books read, most recent first; items are sorted that way. */
static mr_book *read_book(struct app *a, int n)
{
	mr_book *recent[N_RECENT + 1];
	int k = mr_library_recent(&a->lib, recent, N_RECENT + 1);
	return n < k ? recent[n] : NULL;
}

/* Where recent book i's cover goes. */
static int small_x(struct app *a, int i)
{
	(void)a;
	return MARGIN + (i % N_COLS) * (SMALL_W + GAP);
}

static int small_y(int i)
{
	return RECENT_TOP + (i / N_COLS) * CELL_H;
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

	/* Continue reading: one row, tap anywhere on it. */
	mr_text(ctx, pix, MARGIN, HEAD_H + 40, "Continue reading",
		MR_REGULAR, 20, MR_GRAY, 0, MR_LEFT);
	cover_frame(a, b, MARGIN, CARD_TOP, BIG_W, BIG_H);

	int tx = MARGIN + BIG_W + 20, tw = a->w - tx - MARGIN;
	int lines = mr_text_wrap(ctx, pix, tx, CARD_TOP + 26, b->title,
				 MR_BOLD, 24, MR_BLACK, tw, 2, 29);
	mr_text(ctx, pix, tx, CARD_TOP + 26 + lines * 29, b->author,
		MR_REGULAR, 18, MR_GRAY, tw, MR_LEFT);

	int pct = mr_book_percent(b);
	if (pct < 0)
		pct = 0;
	if (pct >= 100)
		snprintf(buf, sizeof buf, "Finished");
	else
		snprintf(buf, sizeof buf, "%d%%", pct);
	int bar_y = CARD_TOP + BIG_H - 12;
	int bar_w = tw - 70;
	mr_fill(ctx, pix, tx, bar_y, bar_w, 6, MR_LIGHT);
	mr_fill(ctx, pix, tx, bar_y, bar_w * pct / 100, 6, MR_BLACK);
	mr_text(ctx, pix, a->w - MARGIN, bar_y + 8, buf, MR_REGULAR, 18,
		MR_GRAY, 0, MR_RIGHT);
	mr_fill(ctx, pix, MARGIN, CARD_TOP + BIG_H + 14, a->w - 2 * MARGIN,
		1, MR_LIGHT);

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
		int x = small_x(a, i), y = small_y(i);
		cover_frame(a, r, x, y, SMALL_W, SMALL_H);
		mr_text_wrap(ctx, pix, x, y + SMALL_H + 22, r->title,
			     MR_REGULAR, 16, MR_BLACK, SMALL_W, 2, 19);
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
			cover_spot(c, small_x(a, i), small_y(i), &cx,
				   &cy);
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

	for (int i = 0; i < N_RECENT; i++) {
		mr_book *r = read_book(a, i + 1);
		int sx = small_x(a, i), sy = small_y(i);
		if (r && x >= sx && x < sx + SMALL_W && y >= sy &&
		    y < sy + CELL_H)
			return open_or_resume(a, r);
	}
	return 0;
}
