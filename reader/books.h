/*
 * The library: every EPUB under a folder, plus a small state file
 * that remembers each book's title, author and the reader's place in
 * it. One line per book, tab-separated, so it's readable with cat.
 * Lines starting with # are settings ("#dark<TAB>1", "#size<TAB>3").
 */
#ifndef MR_BOOKS_H
#define MR_BOOKS_H

#include <mupdf/fitz.h>

typedef struct {
	char *path;
	char *title;
	char *author;

	/*
	 * Size and modification time when the title was read. If
	 * either changes (Calibre rewrote the file), read it again.
	 */
	long long size;
	long long mtime;

	/*
	 * The saved place. chapter is -1 for a book never opened.
	 * chapter_pages is how long that chapter was when the place was
	 * saved, so the place survives a change of text size: page 3 of
	 * 6 becomes page 6 of 12. chapters is the book's total, for the
	 * percentage.
	 */
	int chapter;
	int page;
	int chapter_pages;
	int chapters;

	/*
	 * How far through the book at the saved place, 0-100, worked
	 * out when the place was saved (chapters weighted by size, see
	 * chapters.h). -1 if not known yet.
	 */
	int percent;

	long long last_read;	/* Unix time; 0 = never */
	int seen;		/* found on disk by this run's scan */

	/*
	 * When it was published, "YYYY-MM-DD" or less, from the EPUB;
	 * "" if it doesn't say; NULL if not looked at yet.
	 */
	char *published;

	/*
	 * This book's own reading settings (Settings, Reading, "For:
	 * This book"), in place of the defaults while it's open. Indexes
	 * into the same choices as mr_settings. Only used if custom.
	 */
	int custom;
	struct {
		int size, font, spacing, margin;
	} style;
} mr_book;

/* How the library list is sorted (the Library's Sort button). */
enum {
	MR_SORT_RECENT,		/* last read first, then by title */
	MR_SORT_TITLE,		/* A to Z, "The" and "A" ignored */
	MR_SORT_AUTHOR,		/* by last name */
	MR_SORT_ADDED,		/* newest file first */
	MR_SORT_PUBLISHED,	/* newest first; undated last */
	MR_SORT_COUNT
};

/*
 * Reader settings. Each is an index into a list of choices that lives
 * in settings.c, so the file never holds a value the code can't use.
 */
typedef struct {
	int dark;	/* 0 off, 1 on */
	int size;	/* text size choice; default 2 (9 pt) */
	int font;	/* 0 the book's own, 1 serif, 2 sans */
	int spacing;	/* 0 the book's own, 1 wider, 2 widest */
	int margin;	/* 0 narrow, 1 medium, 2 wide */
	int light;	/* backlight percent; -1 = leave it alone */
	int sleep_after;	/* choice index, default 2 (2 minutes) */
	int clock24;	/* 0 "2:05 PM", 1 "14:05" */
	int sort;	/* MR_SORT_* */
	int pass;	/* the passcode's hash; 0 = no passcode */
	int screen;	/* where you were: 0 Home, 1 Library, 2 Settings,
			   3 the book (system.c) */
	int adb;	/* USB debugging: 0 off (the default), 1 on */
} mr_settings;

typedef struct {
	mr_book *items;
	int count;
	int cap;
	/*
	 * After mr_library_sort, the books found on disk come first:
	 * items[0 .. seen_count - 1]. Books that have gone missing stay
	 * in the state file, so a card that didn't mount doesn't wipe
	 * out every saved place.
	 */
	int seen_count;
	const char *state_path;
	mr_settings set;	/* saved in the same file */
} mr_library;

/* Read the state file. A missing file just means a first run. */
void mr_library_load(mr_library *lib, const char *state_path);

/*
 * Find every .epub under dir. New or changed books are opened once to
 * read their title and author; everything else comes from the state.
 */
void mr_library_scan(fz_context *ctx, mr_library *lib, const char *dir);

/* On disk first; then in the order lib->set.sort picks. */
void mr_library_sort(mr_library *lib);

/*
 * Up to n books on disk that have been read, most recent first, into
 * out, whatever order the library list is in. Returns how many.
 */
int mr_library_recent(const mr_library *lib, mr_book **out, int n);

/* Write the state file safely (see books.c). 0 on success. */
int mr_library_save(const mr_library *lib);

/*
 * How far through the book, 0-100; -1 if never opened. Uses the saved
 * percent; for places saved before that existed, counts every
 * chapter the same.
 */
int mr_book_percent(const mr_book *b);

void mr_library_free(mr_library *lib);

#endif
