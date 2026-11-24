/*
 * The dictionary panel: press and hold a word on the page and its
 * definition comes up over the half of the screen the word isn't on.
 *
 * Finding the word: MuPDF's structured text ("stext") lists every
 * character on the page with its box, line by line. The character
 * nearest the finger in the line under it, widened to the letters
 * around it, is the word.
 *
 * Showing the entry: the dictionary stores each entry as a little
 * HTML, so the panel is laid out by the same engine as the books
 * (wrapping, italics, accented letters), into a picture as tall as
 * the entry, and scrolled like the library list.
 */
#include <stdio.h>
#include <string.h>
#include "app.h"

#define PAD 20		/* around the entry, in pixels */
#define NEAR_PT 6	/* how far from a letter a touch may land */
#define MAX_PT 18	/* the panel follows the text size, up to this */
/*
 * The panel's paper: a light gray, so it stands apart from the page.
 * Dark mode remaps it like everything else: 228 comes out as 41,
 * a little lighter than the page's 20.
 */
#define PANEL_BG 228

static const char panel_css[] =
	"p { margin: 0 }"
	".w { font-size: 1.3em; margin-top: 0.5em }"
	".pr { font-weight: normal; color: #444 }"
	".pos { font-style: italic; color: #444; margin-top: 0.3em }"
	"ol { margin: 0.1em 0 0 0; padding-left: 1.6em }"
	"li { margin: 0.15em 0 }"
	".none { color: #444; margin-top: 0.4em }"
	"hr { margin: 0.8em 0 0.2em 0 }";

/* Letters, digits and what joins them inside a word. */
static int word_char(int c)
{
	if (c < 0x80)
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		       (c >= '0' && c <= '9') || c == '\'' || c == '-';
	if (c == 0x2019 || c == 0xad)	/* curly apostrophe, soft hyphen */
		return 1;
	if (c >= 0x2000 && c <= 0x206f)	/* dashes, quotes, ellipsis */
		return 0;
	return c >= 0xc0;		/* accented and other letters */
}

/*
 * The word at page point p: its UTF-8 in out, its box in *box (page
 * points). Returns 0 if the touch wasn't on a word.
 */
static int word_at(fz_stext_page *st, fz_point p, char *out, size_t n,
		   fz_rect *box)
{
	for (fz_stext_block *b = st->first_block; b; b = b->next) {
		if (b->type != FZ_STEXT_BLOCK_TEXT)
			continue;
		for (fz_stext_line *ln = b->u.t.first_line; ln;
		     ln = ln->next) {
			if (p.y < ln->bbox.y0 - 2 || p.y > ln->bbox.y1 + 2)
				continue;

			/* the character nearest the finger */
			fz_stext_char *best = NULL;
			float best_d = 1e9f;
			for (fz_stext_char *ch = ln->first_char; ch;
			     ch = ch->next) {
				fz_rect r = fz_rect_from_quad(ch->quad);
				float d = p.x < r.x0 ? r.x0 - p.x :
					  p.x > r.x1 ? p.x - r.x1 : 0;
				if (d < best_d) {
					best_d = d;
					best = ch;
				}
			}
			if (!best || best_d > NEAR_PT || !word_char(best->c))
				continue;

			/*
			 * The run of word characters holding it. The
			 * list only links forward, so note where each
			 * run starts and stop after the one we want.
			 */
			fz_stext_char *start = NULL, *end = NULL;
			int passed = 0;
			for (fz_stext_char *ch = ln->first_char; ch;
			     ch = ch->next) {
				if (!word_char(ch->c)) {
					if (passed)
						break;
					start = NULL;
					continue;
				}
				if (!start)
					start = ch;
				if (ch == best)
					passed = 1;
				if (passed)
					end = ch;
			}

			size_t len = 0;
			*box = fz_empty_rect;
			for (fz_stext_char *ch = start; ch; ch = ch->next) {
				char utf[8];
				int k = fz_runetochar(utf, ch->c);
				if (ch->c != 0xad && len + k < n) {
					memcpy(out + len, utf, k);
					len += k;
				}
				*box = fz_union_rect(*box,
					fz_rect_from_quad(ch->quad));
				if (ch == end)
					break;
			}
			out[len] = '\0';
			return len > 0;
		}
	}
	return 0;
}

static void append_escaped(fz_context *ctx, fz_buffer *buf, const char *s)
{
	for (; *s; s++) {
		if (*s == '&')
			fz_append_string(ctx, buf, "&amp;");
		else if (*s == '<')
			fz_append_string(ctx, buf, "&lt;");
		else if (*s == '>')
			fz_append_string(ctx, buf, "&gt;");
		else
			fz_append_byte(ctx, buf, *s);
	}
}

/* The page for the panel: the entry, its base word's, or "not found". */
static fz_buffer *entry_html(struct app *a, const char *word)
{
	fz_context *ctx = a->ctx;
	char key[256];
	const char *see, *html;
	size_t len;

	fz_buffer *buf = fz_new_buffer(ctx, 4096);
	fz_try(ctx) {
		fz_append_string(ctx, buf, "<html><head><style>");
		fz_append_string(ctx, buf, panel_css);
		fz_append_string(ctx, buf, "</style></head><body>");
		if (mr_dict_lookup(a->dict, word, key, sizeof key, &see,
				   &html, &len)) {
			fz_append_data(ctx, buf, html, len);
			/* "ran": show "run" too */
			char base[256];
			snprintf(base, sizeof base, "%s", see);
			if (base[0] && mr_dict_find(a->dict, base, &see,
						    &html, &len)) {
				fz_append_string(ctx, buf, "<hr/>");
				fz_append_data(ctx, buf, html, len);
			}
		} else {
			fz_append_string(ctx, buf, "<p class=\"w\"><b>");
			append_escaped(ctx, buf, word);
			fz_append_string(ctx, buf, "</b></p><p class=\"none\">"
					 "Not in the dictionary.</p>");
		}
		fz_append_string(ctx, buf, "</body></html>");
	}
	fz_catch(ctx) {
		fz_drop_buffer(ctx, buf);
		fz_rethrow(ctx);
	}
	return buf;
}

/*
 * Lay the HTML out a screen wide and as tall as it needs, and draw
 * it into a new picture of exactly that height.
 */
static fz_pixmap *render_entry(struct app *a, fz_buffer *buf)
{
	fz_context *ctx = a->ctx;
	fz_document *doc = NULL;
	fz_page *page = NULL;
	fz_device *dev = NULL;
	fz_pixmap *pix = NULL;
	fz_var(doc);
	fz_var(page);
	fz_var(dev);
	fz_var(pix);

	float pt = setting_font_pt(a);
	if (pt > MAX_PT)
		pt = MAX_PT;
	float scale = SCREEN_DPI / 72.0f;

	fz_try(ctx) {
		doc = fz_open_document_with_buffer(ctx, "text/html", buf);
		/* one very tall page, so the entry never breaks */
		fz_layout_document(ctx, doc, (a->w - 2 * PAD) / scale,
				   30000, pt);
		page = fz_load_page(ctx, doc, 0);

		/* how tall the entry really is: where its ink ends */
		fz_rect ink = fz_empty_rect;
		dev = fz_new_bbox_device(ctx, &ink);
		fz_run_page(ctx, page, dev, fz_identity, NULL);
		fz_close_device(ctx, dev);
		fz_drop_device(ctx, dev);
		dev = NULL;
		int h = fz_is_empty_rect(ink) ? 0 : (int)(ink.y1 * scale);

		pix = fz_new_pixmap(ctx, fz_device_gray(ctx), a->w,
				    h + 2 * PAD, NULL, 0);
		fz_clear_pixmap_with_value(ctx, pix, PANEL_BG);
		fz_matrix ctm = fz_concat(fz_scale(scale, scale),
					  fz_translate(PAD, PAD));
		dev = fz_new_draw_device(ctx, fz_identity, pix);
		fz_run_page(ctx, page, dev, ctm, NULL);
		fz_close_device(ctx, dev);
	}
	fz_always(ctx) {
		fz_drop_device(ctx, dev);
		fz_drop_page(ctx, page);
		fz_drop_document(ctx, doc);
	}
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pix);
		fz_rethrow(ctx);
	}
	return pix;
}

/* The word on the page under screen point (x, y), or 0. */
static int find_word(struct app *a, int x, int y, char *word, size_t n,
		     fz_irect *screen_box)
{
	fz_context *ctx = a->ctx;
	fz_page *page = NULL;
	fz_stext_page *st = NULL;
	int found = 0;
	fz_var(page);
	fz_var(st);
	fz_var(found);

	int tx, ty, tw, th;
	text_box(a, &tx, &ty, &tw, &th);
	float scale = SCREEN_DPI / 72.0f;
	fz_matrix ctm = fz_concat(fz_scale(scale, scale),
				  fz_translate(tx, ty));

	fz_try(ctx) {
		page = fz_load_chapter_page(ctx, a->doc, a->loc.chapter,
					    a->loc.page);
		st = fz_new_stext_page_from_page(ctx, page, NULL);
		fz_point p = fz_make_point((x - tx) / scale,
					   (y - ty) / scale);
		fz_rect box;
		found = word_at(st, p, word, n, &box);
		if (found) {
			/*
			 * A character's box is the font's full height,
			 * which reaches into the lines above and below
			 * when they're set tight. Trim it to the letters.
			 */
			float h = box.y1 - box.y0;
			box.y0 += h * 0.18f;
			box.y1 -= h * 0.08f;
			*screen_box = fz_round_rect(
				fz_transform_rect(box, ctm));
		}
	}
	fz_always(ctx) {
		fz_drop_stext_page(ctx, st);
		fz_drop_page(ctx, page);
	}
	fz_catch(ctx) {
		fprintf(stderr, "lookup: %s\n", fz_caught_message(ctx));
		found = 0;
	}
	return found;
}

void close_dict(struct app *a)
{
	fz_drop_pixmap(a->ctx, a->dict_view.pix);
	a->dict_view.pix = NULL;
	if (a->mode == MODE_DICT)
		a->mode = MODE_BOOK;
}

int lookup_word(struct app *a, int x, int y)
{
	fz_context *ctx = a->ctx;
	char word[256];
	fz_irect box;

	if (!a->doc || !find_word(a, x, y, word, sizeof word, &box))
		return 0;
	if (!a->dict) {
		snprintf(a->status, sizeof a->status,
			 "No dictionary installed");
		return 1;
	}

	fz_buffer *buf = NULL;
	fz_pixmap *pix = NULL;
	fz_var(buf);
	fz_var(pix);
	fz_try(ctx) {
		buf = entry_html(a, word);
		pix = render_entry(a, buf);
	}
	fz_always(ctx)
		fz_drop_buffer(ctx, buf);
	fz_catch(ctx) {
		fprintf(stderr, "lookup %s: %s\n", word,
			fz_caught_message(ctx));
		snprintf(a->status, sizeof a->status,
			 "Couldn't show that word");
		return 1;
	}

	close_dict(a);
	a->dict_view.pix = pix;
	a->dict_view.scroll = 0;
	a->dict_word = box;

	/* the panel covers the half of the screen the word isn't on */
	int max_h = a->h * 55 / 100;
	a->dict_h = pix->h < max_h ? pix->h : max_h;
	int word_y = (box.y0 + box.y1) / 2;
	a->dict_top = word_y > a->h / 2 ? 0 : a->h - a->dict_h;
	a->mode = MODE_DICT;
	return 1;
}

/* Swap black and white inside the word's box, clipped to the screen. */
static void invert_box(struct app *a, fz_irect r)
{
	fz_context *ctx = a->ctx;
	r = fz_intersect_irect(r, fz_make_irect(0, 0, a->w, a->h));
	int stride = fz_pixmap_stride(ctx, a->pix);
	unsigned char *s = fz_pixmap_samples(ctx, a->pix);
	for (int y = r.y0; y < r.y1; y++)
		for (int x = r.x0; x < r.x1; x++)
			s[y * stride + x] = 255 - s[y * stride + x];
}

void draw_dict(struct app *a)
{
	draw_book(a);
	invert_box(a, a->dict_word);

	int top = a->dict_top, h = a->dict_h;
	scroller_show(a, &a->dict_view, top, h);
	/* a rule along the edge that faces the page */
	int edge = top == 0 ? top + h - 2 : top;
	mr_fill(a->ctx, a->pix, 0, edge, a->w, 2, MR_BLACK);
}

static int in_panel(struct app *a, int y)
{
	return y >= a->dict_top && y < a->dict_top + a->dict_h;
}

int on_dict(struct app *a, const struct action *act)
{
	switch (act->type) {
	case ACT_DRAG:
	case ACT_SCROLL:
		return scroller_by(a, &a->dict_view, act->d, a->dict_h);
	case ACT_TAP:
		if (in_panel(a, act->y))
			return 0;
		close_dict(a);	/* a tap on the page puts it away */
		return 1;
	case ACT_HOLD:
		if (in_panel(a, act->y))
			return 0;
		return lookup_word(a, act->x, act->y);
	case ACT_NEXT:
	case ACT_PREV:
	case ACT_SWIPE:
		close_dict(a);
		return on_book(a, act);
	default:
		return 0;
	}
}
