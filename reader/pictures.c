/*
 * Pictures in dark mode. Dark mode remaps every byte of the finished
 * frame (reader.c, apply_dark), which suits text but turns a book's
 * photos into negatives. So after the remap the page is run through
 * MuPDF once more, into a filter device that passes on only "draw
 * this image" and drops everything else: the pictures land again, in
 * their own colors, on top of their negatives.
 *
 * Only opaque pictures (photos, full illustrations) are redrawn. A
 * picture with a transparent background is usually line art, black
 * on clear; redrawn on a dark page it would vanish, while its
 * negative (light lines) reads fine. Whether a picture has any
 * transparent pixel is found by decoding it once; the answer is kept
 * while the book is open.
 */
#include <string.h>
#include "app.h"

#define KNOWN 16

/* Pictures already looked at: kept (fz_keep_image) until dropped. */
static struct {
	fz_image *img;
	int opaque;
} known[KNOWN];
static int next_known;

void drop_pictures(fz_context *ctx)
{
	for (int i = 0; i < KNOWN; i++) {
		fz_drop_image(ctx, known[i].img);
		known[i].img = NULL;
	}
	next_known = 0;
}

/* Does any pixel of the picture let the page show through? */
static int is_opaque(fz_context *ctx, fz_image *img)
{
	if (img->imagemask || img->mask || img->use_colorkey)
		return 0;
	for (int i = 0; i < KNOWN; i++)
		if (known[i].img == img)
			return known[i].opaque;

	int opaque = 1;
	fz_pixmap *pix = NULL;
	fz_var(pix);
	fz_var(opaque);
	fz_try(ctx) {
		pix = fz_get_pixmap_from_image(ctx, img, NULL, NULL, NULL,
					       NULL);
		if (pix->alpha) {
			int n = pix->n;
			for (int y = 0; y < pix->h && opaque; y++) {
				const unsigned char *row =
					pix->samples + (size_t)y * pix->stride;
				for (int x = 0; x < pix->w; x++)
					if (row[x * n + n - 1] != 255) {
						opaque = 0;
						break;
					}
			}
		}
	}
	fz_always(ctx)
		fz_drop_pixmap(ctx, pix);
	fz_catch(ctx)
		opaque = 0;	/* can't tell: leave it as it is */

	fz_drop_image(ctx, known[next_known].img);
	known[next_known].img = fz_keep_image(ctx, img);
	known[next_known].opaque = opaque;
	next_known = (next_known + 1) % KNOWN;
	return opaque;
}

/* The filter: a device that only knows how to draw images. */
struct picture_pass {
	fz_device super;
	fz_device *target;
};

static void pass_image(fz_context *ctx, fz_device *dev, fz_image *img,
		       fz_matrix ctm, float alpha, fz_color_params cp)
{
	struct picture_pass *p = (struct picture_pass *)dev;
	if (is_opaque(ctx, img))
		fz_fill_image(ctx, p->target, img, ctm, alpha, cp);
}

/*
 * Draw the open page's opaque pictures into the frame again, only
 * inside clip (screen pixels): the part of the page that shows.
 */
void redraw_pictures(struct app *a, fz_irect clip)
{
	fz_context *ctx = a->ctx;
	fz_page *page = NULL;
	fz_device *target = NULL;
	struct picture_pass *pass = NULL;
	fz_var(page);
	fz_var(target);
	fz_var(pass);

	if (!a->doc || fz_is_empty_irect(clip))
		return;
	int tx, ty, tw, th;
	text_box(a, &tx, &ty, &tw, &th);
	fz_matrix ctm = fz_concat(fz_scale(SCREEN_DPI / 72.0f,
					   SCREEN_DPI / 72.0f),
				  fz_translate(tx, ty));
	fz_try(ctx) {
		page = fz_load_chapter_page(ctx, a->doc, a->loc.chapter,
					    a->loc.page);
		target = fz_new_draw_device_with_bbox(ctx, fz_identity,
						      a->pix, &clip);
		pass = fz_new_derived_device(ctx, struct picture_pass);
		pass->super.fill_image = pass_image;
		pass->target = target;
		fz_run_page(ctx, page, &pass->super, ctm, NULL);
		fz_close_device(ctx, &pass->super);
		fz_close_device(ctx, target);
	}
	fz_always(ctx) {
		fz_drop_device(ctx, &pass->super);
		fz_drop_device(ctx, target);
		fz_drop_page(ctx, page);
	}
	fz_catch(ctx)
		fprintf(stderr, "pictures: %s\n", fz_caught_message(ctx));
}
