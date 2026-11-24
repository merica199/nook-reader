/*
 * An EPUB is a zip file. Inside it:
 *
 *   META-INF/container.xml  names the package file, e.g. OEBPS/book.opf
 *   the package file        has a <manifest> (id -> file name for every
 *                           file), a <spine> (the reading order, as
 *                           <itemref idref="..."/>), and <metadata>,
 *                           which may say which picture is the cover
 *
 * MuPDF makes one chapter per spine entry, so walking the spine and
 * reading each entry's file gives the chapter sizes in order.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epub.h"

/*
 * Turn a manifest href into a path inside the zip. hrefs are
 * relative to the package file's folder, may be %-encoded
 * ("My%20Chapter.xhtml"), and may climb with "../".
 */
static void resolve(char *out, size_t size, const char *opf_dir,
		    const char *href)
{
	if (opf_dir[0] && strcmp(opf_dir, ".") != 0)
		snprintf(out, size, "%s/%s", opf_dir, href);
	else
		snprintf(out, size, "%s", href);
	char *hash = strchr(out, '#');
	if (hash)
		*hash = 0;
	fz_urldecode(out);
	fz_cleanname(out);
}

/*
 * Find and parse the package file. Returns the parsed XML (drop with
 * fz_drop_xml) and fills opf_dir with its folder. Throws on failure.
 */
static fz_xml *load_opf(fz_context *ctx, fz_archive *arch, char *opf_dir,
			size_t dir_size)
{
	fz_buffer *buf = NULL;
	fz_xml *container = NULL, *opf = NULL;
	fz_var(buf);
	fz_var(container);
	fz_var(opf);

	fz_try(ctx) {
		buf = fz_read_archive_entry(ctx, arch,
					    "META-INF/container.xml");
		container = fz_parse_xml(ctx, buf, 0);
		fz_drop_buffer(ctx, buf);
		buf = NULL;

		fz_xml *rootfile = fz_xml_find_dfs(container, "rootfile",
						   NULL, NULL);
		const char *opf_path = fz_xml_att(rootfile, "full-path");
		if (!opf_path)
			fz_throw(ctx, FZ_ERROR_FORMAT, "no package file");
		fz_dirname(opf_dir, opf_path, dir_size);

		buf = fz_read_archive_entry(ctx, arch, opf_path);
		opf = fz_parse_xml(ctx, buf, 0);
	}
	fz_always(ctx) {
		fz_drop_buffer(ctx, buf);
		fz_drop_xml(ctx, container);
	}
	fz_catch(ctx) {
		fz_drop_xml(ctx, opf);
		fz_rethrow(ctx);
	}
	return opf;
}

/*
 * The size of one file in the EPUB, 0 if it's missing. The fz_try is
 * alone in this function so the loop's locals in mr_chapter_starts
 * aren't near a setjmp: after a longjmp, a local changed since the
 * setjmp is undefined (C11 7.13.2.1), and GCC's -Wclobbered flags
 * every register-held local in reach.
 */
static size_t entry_size(fz_context *ctx, fz_archive *arch,
			 const char *name)
{
	fz_buffer *ch = NULL;
	size_t size = 0;
	fz_var(ch);
	fz_var(size);
	fz_try(ctx) {
		ch = fz_read_archive_entry(ctx, arch, name);
		size = fz_buffer_storage(ctx, ch, NULL);
	}
	fz_always(ctx)
		fz_drop_buffer(ctx, ch);
	fz_catch(ctx)
		size = 0;
	return size;
}

double *mr_chapter_starts(fz_context *ctx, const char *path, int chapters)
{
	fz_archive *arch = NULL;
	fz_xml *opf = NULL;
	double *starts = NULL;
	fz_var(arch);
	fz_var(opf);
	fz_var(starts);

	if (chapters <= 0)
		return NULL;

	fz_try(ctx) {
		char opf_dir[1024];
		arch = fz_open_archive(ctx, path);
		opf = load_opf(ctx, arch, opf_dir, sizeof opf_dir);

		fz_xml *manifest = fz_xml_find_dfs(opf, "manifest",
						   NULL, NULL);
		fz_xml *spine = fz_xml_find_dfs(opf, "spine", NULL, NULL);
		if (!manifest || !spine)
			fz_throw(ctx, FZ_ERROR_FORMAT, "no spine");

		starts = calloc(chapters + 1, sizeof(double));
		if (!starts)
			fz_throw(ctx, FZ_ERROR_SYSTEM, "out of memory");

		/*
		 * First pass: each chapter's size goes in starts[i + 1].
		 * A second pass below turns sizes into running totals.
		 */
		int n = 0;
		for (fz_xml *ref = fz_xml_find_down(spine, "itemref");
		     ref; ref = fz_xml_find_next(ref, "itemref")) {
			if (n == chapters) {
				n++;	/* more spine entries than chapters */
				break;
			}
			const char *idref = fz_xml_att(ref, "idref");
			fz_xml *item = idref ? fz_xml_find_down_match(
				manifest, "item", "id", idref) : NULL;
			const char *href = item ?
				fz_xml_att(item, "href") : NULL;

			/*
			 * A chapter whose file is missing still counts a
			 * little, so it never has zero width.
			 */
			double size = 1;
			if (href) {
				char name[2048];
				resolve(name, sizeof name, opf_dir, href);
				size += entry_size(ctx, arch, name);
			}
			starts[++n] = size;
		}
		if (n != chapters)
			fz_throw(ctx, FZ_ERROR_FORMAT,
				 "spine has %d entries, MuPDF %d chapters",
				 n, chapters);

		/* Sizes to running totals, then to fractions of 1. */
		for (int i = 1; i <= chapters; i++)
			starts[i] += starts[i - 1];
		double total = starts[chapters];
		for (int i = 1; i <= chapters; i++)
			starts[i] /= total;
	}
	fz_always(ctx) {
		fz_drop_xml(ctx, opf);
		fz_drop_archive(ctx, arch);
	}
	fz_catch(ctx) {
		fprintf(stderr, "%s: chapter sizes: %s\n", path,
			fz_caught_message(ctx));
		free(starts);
		starts = NULL;
	}
	return starts;
}

static int is_image(fz_xml *item)
{
	const char *type = fz_xml_att(item, "media-type");
	return type && strncmp(type, "image/", 6) == 0;
}

/*
 * Which manifest item is the cover. Books say it three ways, newest
 * first:
 *   EPUB 3:  <item properties="cover-image" .../>
 *   EPUB 2:  <meta name="cover" content="the item's id"/>
 *   neither: an image whose id or file name says "cover"
 */
static fz_xml *find_cover_item(fz_xml *opf, fz_xml *manifest)
{
	fz_xml *item;

	for (item = fz_xml_find_down(manifest, "item"); item;
	     item = fz_xml_find_next(item, "item")) {
		const char *props = fz_xml_att(item, "properties");
		if (props && strstr(props, "cover-image"))
			return item;
	}

	fz_xml *meta = fz_xml_find_dfs(opf, "meta", "name", "cover");
	const char *id = meta ? fz_xml_att(meta, "content") : NULL;
	if (id) {
		item = fz_xml_find_down_match(manifest, "item", "id", id);
		if (item && is_image(item))
			return item;
	}

	for (item = fz_xml_find_down(manifest, "item"); item;
	     item = fz_xml_find_next(item, "item")) {
		const char *iid = fz_xml_att(item, "id");
		const char *href = fz_xml_att(item, "href");
		if (is_image(item) &&
		    ((iid && strstr(iid, "cover")) ||
		     (href && strstr(href, "cover"))))
			return item;
	}
	return NULL;
}

fz_image *mr_epub_cover(fz_context *ctx, const char *path)
{
	fz_archive *arch = NULL;
	fz_xml *opf = NULL;
	fz_buffer *buf = NULL;
	fz_image *img = NULL;
	fz_var(arch);
	fz_var(opf);
	fz_var(buf);
	fz_var(img);

	fz_try(ctx) {
		char opf_dir[1024];
		arch = fz_open_archive(ctx, path);
		opf = load_opf(ctx, arch, opf_dir, sizeof opf_dir);

		fz_xml *manifest = fz_xml_find_dfs(opf, "manifest",
						   NULL, NULL);
		fz_xml *item = manifest ? find_cover_item(opf, manifest)
					: NULL;
		const char *href = item ? fz_xml_att(item, "href") : NULL;
		if (href) {
			char name[2048];
			resolve(name, sizeof name, opf_dir, href);
			buf = fz_read_archive_entry(ctx, arch, name);
			img = fz_new_image_from_buffer(ctx, buf);
		}
	}
	fz_always(ctx) {
		fz_drop_buffer(ctx, buf);
		fz_drop_xml(ctx, opf);
		fz_drop_archive(ctx, arch);
	}
	fz_catch(ctx) {
		fz_drop_image(ctx, img);
		img = NULL;
	}
	return img;
}
