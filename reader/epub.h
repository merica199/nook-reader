/*
 * Reading an EPUB's own files directly, for the two things MuPDF's
 * document interface doesn't give us: how long each chapter is, and
 * the cover picture.
 */
#ifndef MR_EPUB_H
#define MR_EPUB_H

#include <mupdf/fitz.h>

/*
 * Where each chapter starts, as a fraction of the whole book:
 * starts[i] is the share of the book before chapter i, and
 * starts[chapters] is 1. Chapters are weighed by the size of their
 * files: a 40-page chapter's file is about 40 times a title page's.
 * Free with free(). NULL if the spine doesn't line up with MuPDF's
 * chapters; callers then count every chapter the same.
 */
double *mr_chapter_starts(fz_context *ctx, const char *path, int chapters);

/*
 * The book's cover picture, or NULL if it doesn't name one. Drop with
 * fz_drop_image.
 */
fz_image *mr_epub_cover(fz_context *ctx, const char *path);

/*
 * When the book was published (dc:date), as "YYYY-MM-DD" or as much
 * of it as the book gives, into buf; empty if it doesn't say.
 */
void mr_epub_date(fz_context *ctx, const char *path, char *buf,
		  size_t n);

#endif
