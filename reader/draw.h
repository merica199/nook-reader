/*
 * Drawing the reader's own screens (the library, the footer under a
 * page) into the same gray pixmap the book pages go into. Text uses
 * MuPDF's built-in Helvetica, so the Nook needs no font files.
 */
#ifndef MR_DRAW_H
#define MR_DRAW_H

#include <mupdf/fitz.h>

/* Gray levels: 0 is black, 255 is white. */
#define MR_BLACK 0
#define MR_GRAY 100
#define MR_LIGHT 190
#define MR_WHITE 255

typedef enum { MR_REGULAR, MR_BOLD } mr_weight;
typedef enum { MR_LEFT, MR_CENTER, MR_RIGHT } mr_align;

void mr_draw_init(fz_context *ctx);
void mr_draw_done(fz_context *ctx);

/* A filled rectangle, x/y its top left corner. */
/* A new picture to draw in: RGB, no alpha. */
fz_pixmap *mr_new_pixmap(fz_context *ctx, int w, int h);

void mr_fill(fz_context *ctx, fz_pixmap *pix, int x, int y, int w, int h,
	     int gray);

/* Width in pixels of s (UTF-8) at size pixels. */
float mr_text_width(fz_context *ctx, const char *s, mr_weight wt,
		    float size);

/*
 * One line of text with its baseline at y. x is the left edge, the
 * middle, or the right edge depending on align. If max_w > 0 and the
 * text is wider, it's cut short with an ellipsis. Throws on failure
 * (out of memory), like MuPDF's own calls.
 */
void mr_text(fz_context *ctx, fz_pixmap *pix, float x, float y,
	     const char *s, mr_weight wt, float size, int gray,
	     float max_w, mr_align align);

/*
 * Text wrapped at spaces into lines no wider than max_w, at most
 * max_lines of them, line_h pixels apart, the first baseline at y.
 * The last line gets an ellipsis if text is left over. Returns the
 * number of lines drawn.
 */
int mr_text_wrap(fz_context *ctx, fz_pixmap *pix, float x, float y,
		 const char *s, mr_weight wt, float size, int gray,
		 float max_w, int max_lines, float line_h);

/* A rectangle's outline, t pixels thick. */
/* A lightning bolt in the box x, y, w, h (the charging sign). */
void mr_bolt(fz_context *ctx, fz_pixmap *pix, float x, float y, float w,
	     float h, int gray);

void mr_outline(fz_context *ctx, fz_pixmap *pix, int x, int y, int w,
		int h, int t, int gray);

#endif
