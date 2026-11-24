/*
 * Settings, on two pages:
 *
 *   Reading  text size, font, line spacing, margins, and a paragraph
 *            laid out by the same engine with the same settings, so
 *            it shows exactly what a book will look like
 *   Device   dark mode, brightness, dim after, sleep after, 12 or 24
 *            hour clock, and setting the date and time
 *
 * Rows the device can't do (no backlight, no clock to set) are left
 * off. Every setting is an index into a list of choices here, so the
 * state file never holds a value the code can't use.
 */
#include <stdio.h>
#include <string.h>
#include "app.h"

/* ---- the choices ---- */

/*
 * The state file saves an index into this list, so new sizes go on
 * the end: an old index must keep meaning the same size.
 */
static const float sizes_pt[] = {
	7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28
};
#define N_SIZES (int)(sizeof sizes_pt / sizeof *sizes_pt)
/*
 * The page margins, in screen pixels, so they stay put at every text
 * size. MuPDF's own page margins are switched off (page_css): they're
 * in em, so they grew with the text, to 124 px a side at 28 pt.
 */
static const int margins_px[] = { 32, 48, 72 };
static const char page_css[] = "@page { margin: 0 !important; }";

static const char *font_css[] = {
	"", "* { font-family: serif !important; }",
	"* { font-family: sans-serif !important; }",
};
static const char *spacing_css[] = {
	"", "* { line-height: 1.45 !important; }",
	"* { line-height: 1.75 !important; }",
};

/* Dim and sleep after this many minutes of nothing (0: never). */
static const int dim_min[] = { 0, 1, 2, 5 };
static const int sleep_min[] = { 0, 5, 10, 15, 30 };
#define COUNT(a) (int)(sizeof a / sizeof *a)

static int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

float setting_font_pt(const struct app *a)
{
	return sizes_pt[clamp(a->lib.set.size, 0, N_SIZES - 1)];
}

int setting_margin_px(const struct app *a)
{
	return margins_px[clamp(a->lib.set.margin, 0, 2)];
}

long long setting_dim_ms(const struct app *a)
{
	return dim_min[clamp(a->lib.set.dim, 0, COUNT(dim_min) - 1)] *
	       60000LL;
}

long long setting_sleep_ms(const struct app *a)
{
	return sleep_min[clamp(a->lib.set.sleep, 0,
			       COUNT(sleep_min) - 1)] * 60000LL;
}

/*
 * Hand MuPDF the user stylesheet for font and spacing. It's read when
 * a document's text is laid out, which is why leaving Settings
 * re-opens the book (ui_go).
 */
void apply_settings(struct app *a)
{
	char css[256];
	snprintf(css, sizeof css, "%s %s %s", page_css,
		 font_css[clamp(a->lib.set.font, 0, 2)],
		 spacing_css[clamp(a->lib.set.spacing, 0, 2)]);
	fz_set_user_css(a->ctx, css);
}

/* ---- the rows ---- */

enum kind {
	STEP,	/* "-  value  +" */
	SEG,	/* one of a few choices, side by side */
	BUTTON,	/* opens another screen */
};

enum row_id {
	R_SIZE, R_FONT, R_SPACING, R_MARGIN,
	R_DARK, R_LIGHT, R_DIM, R_SLEEP, R_CLOCK24, R_SETTIME,
};

struct row {
	enum row_id id;
	enum kind kind;
	const char *label;
	const char *seg[5];
};

static const struct row reading_rows[] = {
	{ R_SIZE, STEP, "Text size", { 0 } },
	{ R_FONT, SEG, "Font", { "Book", "Serif", "Sans" } },
	{ R_SPACING, SEG, "Line spacing", { "Book", "Wider", "Widest" } },
	{ R_MARGIN, SEG, "Margins", { "Narrow", "Medium", "Wide" } },
};

static const struct row device_rows[] = {
	{ R_DARK, SEG, "Dark mode", { "Off", "On" } },
	{ R_LIGHT, STEP, "Brightness", { 0 } },
	{ R_DIM, SEG, "Dim after", { "Off", "1 min", "2", "5" } },
	{ R_SLEEP, SEG, "Sleep after", { "Off", "5 min", "10", "15", "30" } },
	{ R_CLOCK24, SEG, "Clock", { "2:05 PM", "14:05" } },
	{ R_SETTIME, BUTTON, "Date & time", { "Set" } },
};

static int has_light(struct app *a)
{
	return a->plat->light && a->plat->light(-1) >= 0;
}

/* The rows on the page showing, minus what this device can't do. */
static int page_rows(struct app *a, const struct row **out)
{
	const struct row *src = a->settings_page ? device_rows
						  : reading_rows;
	int n = a->settings_page ? COUNT(device_rows) : COUNT(reading_rows);
	int k = 0;
	for (int i = 0; i < n; i++) {
		if ((src[i].id == R_LIGHT || src[i].id == R_DIM) &&
		    !has_light(a))
			continue;
		if (src[i].id == R_SETTIME && !a->plat->set_time)
			continue;
		out[k++] = &src[i];
	}
	return k;
}

static int *row_value(struct app *a, enum row_id id)
{
	switch (id) {
	case R_SIZE:
		return &a->lib.set.size;
	case R_FONT:
		return &a->lib.set.font;
	case R_SPACING:
		return &a->lib.set.spacing;
	case R_MARGIN:
		return &a->lib.set.margin;
	case R_DARK:
		return &a->lib.set.dark;
	case R_DIM:
		return &a->lib.set.dim;
	case R_SLEEP:
		return &a->lib.set.sleep;
	case R_CLOCK24:
		return &a->lib.set.clock24;
	default:
		return &a->lib.set.light;
	}
}

/* ---- layout ---- */

#define PAGES_Y (HEAD_H + 14)	/* the Reading / Device switch */
#define PAGES_H 56
#define ROW_TOP (PAGES_Y + PAGES_H + 10)
#define ROW_H 92
#define LABEL_W 180
#define CTRL_X (MARGIN + LABEL_W)
#define BTN_H 56

static int seg_count(const struct row *r)
{
	int n = 0;
	while (n < 5 && r->seg[n])
		n++;
	return n;
}

/*
 * Button i of row k: a segment, or for STEP rows 0 = minus and
 * 1 = plus, or the one BUTTON.
 */
static void btn_box(struct app *a, const struct row *r, int k, int i,
		    int *x, int *y, int *w)
{
	int ctrl_w = a->w - MARGIN - CTRL_X;
	*y = ROW_TOP + k * ROW_H + (ROW_H - BTN_H) / 2;
	if (r->kind == STEP) {
		*w = 72;
		*x = i == 0 ? CTRL_X : CTRL_X + ctrl_w - 72;
	} else if (r->kind == BUTTON) {
		*w = 140;
		*x = CTRL_X + ctrl_w - 140;
	} else {
		int n = seg_count(r), gap = 8;
		*w = (ctrl_w - (n - 1) * gap) / n;
		*x = CTRL_X + i * (*w + gap);
	}
}

static void page_box(struct app *a, int i, int *x, int *w)
{
	*w = (a->w - 2 * MARGIN - 8) / 2;
	*x = MARGIN + i * (*w + 8);
}

/* "Done" in the header, when a book is open: back to the page. */
static int done_hit(struct app *a, int x, int y)
{
	return a->doc && y < HEAD_H && x > a->w - 160;
}

/* ---- the preview ---- */

static const char preview_html[] =
	"<html><body><p>This is how your books will look. Change the "
	"size, font, spacing and margins above, and this paragraph "
	"follows along, set by the same engine that lays out the "
	"books.</p><p>The quick brown fox jumps over the lazy dog.</p>"
	"</body></html>";

static void draw_preview(struct app *a, int top, int bottom)
{
	fz_context *ctx = a->ctx;
	fz_buffer *buf = NULL;
	fz_document *doc = NULL;
	fz_page *page = NULL;
	fz_device *dev = NULL;
	fz_var(buf);
	fz_var(doc);
	fz_var(page);
	fz_var(dev);

	int m = setting_margin_px(a);
	int x = m, w = a->w - 2 * m, h = bottom - top - 16;
	if (h < 40)
		return;
	mr_fill(ctx, a->pix, 0, top, a->w, 1, MR_LIGHT);

	fz_try(ctx) {
		buf = fz_new_buffer_from_copied_data(ctx,
			(const unsigned char *)preview_html,
			strlen(preview_html));
		doc = fz_open_document_with_buffer(ctx, "text/html", buf);
		fz_layout_document(ctx, doc, w * 72.0f / SCREEN_DPI,
				   h * 72.0f / SCREEN_DPI,
				   setting_font_pt(a));
		page = fz_load_page(ctx, doc, 0);
		fz_matrix ctm = fz_concat(fz_scale(SCREEN_DPI / 72.0f,
						   SCREEN_DPI / 72.0f),
					  fz_translate(x, top + 8));
		dev = fz_new_draw_device(ctx, fz_identity, a->pix);
		fz_run_page(ctx, page, dev, ctm, NULL);
		fz_close_device(ctx, dev);
	}
	fz_always(ctx) {
		fz_drop_device(ctx, dev);
		fz_drop_page(ctx, page);
		fz_drop_document(ctx, doc);
		fz_drop_buffer(ctx, buf);
	}
	fz_catch(ctx)
		fprintf(stderr, "preview: %s\n", fz_caught_message(ctx));
}

/* ---- the screen ---- */

static void step_value(struct app *a, enum row_id id, char *buf,
		       size_t n)
{
	if (id == R_SIZE)
		snprintf(buf, n, "%g pt", setting_font_pt(a));
	else
		snprintf(buf, n, "%d%%", a->plat->light(-1));
}

void draw_settings(struct app *a)
{
	fz_context *ctx = a->ctx;
	const struct row *rows[8];
	char buf[32];

	fz_clear_pixmap_with_value(ctx, a->pix, MR_WHITE);
	ui_header_to(a, "Settings",
		     a->doc ? a->w - MARGIN - 126 : a->w - MARGIN);
	if (a->doc)
		ui_button(a, a->w - MARGIN - 110, 22, 110, 52, "Done", 0);

	static const char *pages[] = { "Reading", "Device" };
	for (int i = 0; i < 2; i++) {
		int x, w;
		page_box(a, i, &x, &w);
		ui_button(a, x, PAGES_Y, w, PAGES_H, pages[i],
			  i == a->settings_page);
	}

	int n = page_rows(a, rows);
	for (int k = 0; k < n; k++) {
		const struct row *r = rows[k];
		int y = ROW_TOP + k * ROW_H;
		mr_text(ctx, a->pix, MARGIN, y + ROW_H / 2 + 8, r->label,
			MR_BOLD, 22, MR_BLACK, LABEL_W - 8, MR_LEFT);

		int bx, by, bw;
		if (r->kind == STEP) {
			step_value(a, r->id, buf, sizeof buf);
			btn_box(a, r, k, 0, &bx, &by, &bw);
			ui_button(a, bx, by, bw, BTN_H, "-", 0);
			int mid = (CTRL_X + a->w - MARGIN) / 2;
			mr_text(ctx, a->pix, mid, by + BTN_H / 2 + 8, buf,
				MR_BOLD, 24, MR_BLACK, 0, MR_CENTER);
			btn_box(a, r, k, 1, &bx, &by, &bw);
			ui_button(a, bx, by, bw, BTN_H, "+", 0);
		} else if (r->kind == BUTTON) {
			/* the date and time now, then "Set" */
			char now[64];
			btn_box(a, r, k, 0, &bx, &by, &bw);
			ui_date_time(a, time(NULL), now, sizeof now);
			mr_text(ctx, a->pix, CTRL_X, by + BTN_H / 2 + 8, now,
				MR_REGULAR, 20, MR_GRAY,
				bx - CTRL_X - 8, MR_LEFT);
			ui_button(a, bx, by, bw, BTN_H, r->seg[0], 0);
		} else {
			int cur = *row_value(a, r->id);
			for (int i = 0; i < seg_count(r); i++) {
				btn_box(a, r, k, i, &bx, &by, &bw);
				ui_button(a, bx, by, bw, BTN_H, r->seg[i],
					  i == cur);
			}
		}
		mr_fill(ctx, a->pix, MARGIN, y + ROW_H - 1,
			a->w - 2 * MARGIN, 1, MR_LIGHT);
	}

	if (a->settings_page == 0)
		draw_preview(a, ROW_TOP + n * ROW_H + 8, a->h - TAB_H);
	ui_tabs(a);
}

int on_settings(struct app *a, const struct action *act)
{
	const struct row *rows[8];
	if (act->type != ACT_TAP)
		return 0;
	int x = act->x, y = act->y;

	enum mode to;
	if (ui_tab_hit(a, x, y, &to)) {
		if (to == MODE_SETTINGS)
			return 0;
		ui_go(a, to);
		return 1;
	}
	if (done_hit(a, x, y)) {
		ui_go(a, MODE_BOOK);
		return 1;
	}
	if (y >= PAGES_Y && y < PAGES_Y + PAGES_H) {
		int px, pw;
		page_box(a, 1, &px, &pw);
		int page = x >= px;
		if (page == a->settings_page)
			return 0;
		a->settings_page = page;
		return 1;
	}

	int n = page_rows(a, rows);
	int k = (y - ROW_TOP) / ROW_H;
	if (y < ROW_TOP || k >= n)
		return 0;
	const struct row *r = rows[k];

	int count = r->kind == SEG ? seg_count(r) :
		    r->kind == STEP ? 2 : 1;
	for (int i = 0; i < count; i++) {
		int bx, by, bw;
		btn_box(a, r, k, i, &bx, &by, &bw);
		if (x < bx || x >= bx + bw || y < by || y >= by + BTN_H)
			continue;

		int *v = row_value(a, r->id);
		if (r->id == R_SETTIME) {
			open_clock(a);
			return 1;
		} else if (r->id == R_LIGHT) {
			int now = a->plat->light(-1);
			*v = a->plat->light(now + (i ? 10 : -10));
		} else if (r->id == R_SIZE) {
			*v = clamp(*v + (i ? 1 : -1), 0, N_SIZES - 1);
		} else {
			*v = i;
		}

		/* only the Reading page changes how the text is laid out */
		if (a->settings_page == 0)
			a->relayout = 1;
		apply_settings(a);
		mr_library_save(&a->lib);
		return 1;
	}
	return 0;
}
