#include <string.h>
#include "draw.h"

static fz_font *regular, *bold;

/* U+2026, the one-character ellipsis, in UTF-8 */
static const char ELLIPSIS[] = "\xe2\x80\xa6";

void mr_draw_init(fz_context *ctx)
{
	regular = fz_new_base14_font(ctx, "Helvetica");
	bold = fz_new_base14_font(ctx, "Helvetica-Bold");
}

void mr_draw_done(fz_context *ctx)
{
	fz_drop_font(ctx, regular);
	fz_drop_font(ctx, bold);
}

static fz_font *pick(mr_weight wt)
{
	return wt == MR_BOLD ? bold : regular;
}

/*
 * Every picture the reader draws in: RGB, 3 bytes a pixel, no alpha.
 * The interface is gray, which in RGB is just the same byte three
 * times; book pages and covers come out in their own colors.
 */
fz_pixmap *mr_new_pixmap(fz_context *ctx, int w, int h)
{
	return fz_new_pixmap(ctx, fz_device_rgb(ctx), w, h, NULL, 0);
}

/*
 * MuPDF has fz_clear_pixmap_rect_with_value for this, but in 1.27.2
 * it leaves a gray pixmap untouched (tested 2026-11-23), so do it by
 * hand: clip the rectangle to the pixmap, then memset each row. A
 * gray level is the same value in every channel, so one memset
 * covers all of them.
 */
void mr_fill(fz_context *ctx, fz_pixmap *pix, int x, int y, int w, int h,
	     int gray)
{
	int pw = fz_pixmap_width(ctx, pix);
	int ph = fz_pixmap_height(ctx, pix);
	int stride = fz_pixmap_stride(ctx, pix);
	int n = fz_pixmap_components(ctx, pix);
	unsigned char *s = fz_pixmap_samples(ctx, pix);

	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + w > pw ? pw : x + w;
	int y1 = y + h > ph ? ph : y + h;
	for (int row = y0; row < y1 && x0 < x1; row++)
		memset(s + row * stride + x0 * n, gray, (x1 - x0) * n);
}

/*
 * Advance of one character, in pixels. Fonts store advances in units
 * of the font size, so multiply by the size.
 */
static float advance(fz_context *ctx, fz_font *font, int rune, float size)
{
	int gid = fz_encode_character(ctx, font, rune);
	return fz_advance_glyph(ctx, font, gid, 0) * size;
}

float mr_text_width(fz_context *ctx, const char *s, mr_weight wt,
		    float size)
{
	fz_font *font = pick(wt);
	float w = 0;
	while (*s) {
		int rune;
		s += fz_chartorune(&rune, s);
		w += advance(ctx, font, rune, size);
	}
	return w;
}

/*
 * Copy s into out, cut short with an ellipsis if it's wider than
 * max_w. Walks the string one UTF-8 character at a time so a cut
 * never lands in the middle of a multi-byte character.
 */
static void fit(fz_context *ctx, const char *s, mr_weight wt, float size,
		float max_w, char *out, size_t out_size)
{
	size_t len;

	if (max_w <= 0 || mr_text_width(ctx, s, wt, size) <= max_w) {
		len = strlen(s);
		if (len > out_size - 1)
			len = out_size - 1;
		memcpy(out, s, len);
		out[len] = 0;
		return;
	}

	fz_font *font = pick(wt);
	float room = max_w - mr_text_width(ctx, ELLIPSIS, wt, size);
	float w = 0;
	const char *p = s, *cut = s;
	while (*p) {
		int rune;
		int n = fz_chartorune(&rune, p);
		w += advance(ctx, font, rune, size);
		if (w > room)
			break;
		p += n;
		cut = p;
	}

	len = cut - s;
	/* "Ordinary Men…", not "Ordinary Men …" */
	while (len > 0 && s[len - 1] == ' ')
		len--;
	if (len > out_size - sizeof ELLIPSIS)
		len = out_size - sizeof ELLIPSIS;
	memcpy(out, s, len);
	memcpy(out + len, ELLIPSIS, sizeof ELLIPSIS);
}

void mr_outline(fz_context *ctx, fz_pixmap *pix, int x, int y, int w,
		int h, int t, int gray)
{
	mr_fill(ctx, pix, x, y, w, t, gray);
	mr_fill(ctx, pix, x, y + h - t, w, t, gray);
	mr_fill(ctx, pix, x, y, t, h, gray);
	mr_fill(ctx, pix, x + w - t, y, t, h, gray);
}

/*
 * A lightning bolt filling the box x, y, w, h: a filled path, drawn
 * by MuPDF like a page's own shapes. The points are fractions of the
 * box, clockwise from the top.
 */
void mr_bolt(fz_context *ctx, fz_pixmap *pix, float x, float y, float w,
	     float h, int gray)
{
	static const float pt[][2] = {
		{ 0.62f, 0.00f }, { 0.18f, 0.56f }, { 0.46f, 0.56f },
		{ 0.36f, 1.00f }, { 0.82f, 0.42f }, { 0.54f, 0.42f },
	};
	fz_path *path = NULL;
	fz_device *dev = NULL;
	fz_var(path);
	fz_var(dev);
	fz_try(ctx) {
		path = fz_new_path(ctx);
		for (size_t i = 0; i < sizeof pt / sizeof *pt; i++) {
			float px = x + pt[i][0] * w, py = y + pt[i][1] * h;
			if (i == 0)
				fz_moveto(ctx, path, px, py);
			else
				fz_lineto(ctx, path, px, py);
		}
		fz_closepath(ctx, path);
		dev = fz_new_draw_device(ctx, fz_identity, pix);
		float color = gray / 255.0f;
		fz_fill_path(ctx, dev, path, 0, fz_identity,
			     fz_device_gray(ctx), &color, 1.0f,
			     fz_default_color_params);
		fz_close_device(ctx, dev);
	}
	fz_always(ctx) {
		fz_drop_device(ctx, dev);
		fz_drop_path(ctx, path);
	}
	fz_catch(ctx)
		fz_rethrow(ctx);
}

int mr_text_wrap(fz_context *ctx, fz_pixmap *pix, float x, float y,
		 const char *s, mr_weight wt, float size, int gray,
		 float max_w, int max_lines, float line_h)
{
	char line[512];
	int lines = 0;

	while (*s && lines < max_lines) {
		while (*s == ' ')
			s++;
		/*
		 * Grow the line a word at a time until the next word
		 * wouldn't fit. The last allowed line takes everything
		 * left and lets mr_text cut it with an ellipsis.
		 */
		size_t len = 0;
		if (lines == max_lines - 1) {
			len = strlen(s);
		} else {
			size_t try = 0;
			while (s[try]) {
				size_t end = try;
				while (s[end] == ' ')
					end++;
				while (s[end] && s[end] != ' ')
					end++;
				if (end >= sizeof line)
					break;
				memcpy(line, s, end);
				line[end] = 0;
				if (len > 0 && mr_text_width(ctx, line, wt,
							     size) > max_w)
					break;
				len = end;
				try = end;
			}
			if (len == 0)	/* one word wider than the line */
				len = strcspn(s, " ");
		}
		if (len >= sizeof line)
			len = sizeof line - 1;
		memcpy(line, s, len);
		line[len] = 0;
		mr_text(ctx, pix, x, y + lines * line_h, line, wt, size,
			gray, max_w, MR_LEFT);
		s += len;
		lines++;
	}
	return lines;
}

/*
 * Draw s with its baseline starting at (x, y). Separate from mr_text
 * so the fz_try has no other locals near it: after a longjmp, a local
 * changed since the setjmp is undefined (C11 7.13.2.1), and GCC's
 * -Wclobbered flags every register-held local in reach.
 */
static void draw_string(fz_context *ctx, fz_pixmap *pix, float x,
			float y, const char *buf, mr_weight wt, float size,
			int gray)
{
	fz_text *text = NULL;
	fz_device *dev = NULL;
	fz_var(text);
	fz_var(dev);

	fz_try(ctx) {
		text = fz_new_text(ctx);
		/*
		 * Glyphs are designed y-up, the screen is y-down, so the
		 * text matrix flips y with -size and puts the baseline
		 * origin at (x, y).
		 */
		fz_matrix trm = fz_make_matrix(size, 0, 0, -size, x, y);
		fz_show_string(ctx, text, pick(wt), trm, buf, 0, 0,
			       FZ_BIDI_LTR, FZ_LANG_UNSET);
		dev = fz_new_draw_device(ctx, fz_identity, pix);
		float color = gray / 255.0f;
		fz_fill_text(ctx, dev, text, fz_identity,
			     fz_device_gray(ctx), &color, 1.0f,
			     fz_default_color_params);
		fz_close_device(ctx, dev);
	}
	fz_always(ctx) {
		fz_drop_device(ctx, dev);
		fz_drop_text(ctx, text);
	}
	fz_catch(ctx)
		fz_rethrow(ctx);
}

void mr_text(fz_context *ctx, fz_pixmap *pix, float x, float y,
	     const char *s, mr_weight wt, float size, int gray,
	     float max_w, mr_align align)
{
	char buf[512];
	fit(ctx, s, wt, size, max_w, buf, sizeof buf);

	float w = mr_text_width(ctx, buf, wt, size);
	if (align == MR_RIGHT)
		x -= w;
	else if (align == MR_CENTER)
		x -= w / 2;
	draw_string(ctx, pix, x, y, buf, wt, size, gray);
}
