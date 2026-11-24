#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "books.h"
#include "epub.h"

/*
 * First line of the state file, then the format version. Version 2
 * added the percent as an 11th field and # settings lines; version 1
 * files still load (their percent is worked out the old way).
 */
#define STATE_MAGIC "myreader-state "
#define STATE_VERSION 3
#define STATE_FIELDS 12

/* How deep to look for books: Calibre uses author/title/book.epub. */
#define MAX_DEPTH 6

/* strdup, with tabs and newlines turned into spaces for the file. */
static char *dup_clean(const char *s)
{
	char *d = strdup(s);
	if (!d) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	for (char *p = d; *p; p++)
		if (*p == '\t' || *p == '\n' || *p == '\r')
			*p = ' ';
	return d;
}

static mr_book *add_book(mr_library *lib)
{
	if (lib->count == lib->cap) {
		lib->cap = lib->cap ? lib->cap * 2 : 64;
		lib->items = realloc(lib->items,
				     lib->cap * sizeof(mr_book));
		if (!lib->items) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
	}
	mr_book *b = &lib->items[lib->count++];
	memset(b, 0, sizeof *b);
	b->chapter = -1;
	b->percent = -1;
	return b;
}

static mr_book *find_book(mr_library *lib, const char *path)
{
	for (int i = 0; i < lib->count; i++)
		if (strcmp(lib->items[i].path, path) == 0)
			return &lib->items[i];
	return NULL;
}

/* The settings and where each lives in the struct. */
static const struct {
	const char *key;
	size_t offset;
} setting_keys[] = {
	{ "dark", offsetof(mr_settings, dark) },
	{ "size", offsetof(mr_settings, size) },
	{ "font", offsetof(mr_settings, font) },
	{ "spacing", offsetof(mr_settings, spacing) },
	{ "margin", offsetof(mr_settings, margin) },
	{ "light", offsetof(mr_settings, light) },
	{ "dim", offsetof(mr_settings, dim) },
	{ "sleep", offsetof(mr_settings, sleep) },
	{ "clock24", offsetof(mr_settings, clock24) },
	{ "sort", offsetof(mr_settings, sort) },
	{ "pass", offsetof(mr_settings, pass) },
};
#define N_SETTINGS (sizeof setting_keys / sizeof *setting_keys)

/* "key<TAB>value": set the matching field; ignore unknown keys. */
static void read_setting(mr_settings *set, const char *line)
{
	const char *tab = strchr(line, '\t');
	if (!tab)
		return;
	for (size_t i = 0; i < N_SETTINGS; i++) {
		size_t n = strlen(setting_keys[i].key);
		if ((size_t)(tab - line) == n &&
		    strncmp(line, setting_keys[i].key, n) == 0)
			*(int *)((char *)set + setting_keys[i].offset) =
				atoi(tab + 1);
	}
}

void mr_library_load(mr_library *lib, const char *state_path)
{
	memset(lib, 0, sizeof *lib);
	lib->state_path = state_path;
	lib->set.size = 2;
	lib->set.dark = 1;
	lib->set.light = -1;
	lib->set.dim = 2;
	lib->set.sleep = 2;

	FILE *f = fopen(state_path, "r");
	if (!f)
		return;

	char line[4096];
	size_t magic = strlen(STATE_MAGIC);
	int version = 0;
	if (fgets(line, sizeof line, f) &&
	    strncmp(line, STATE_MAGIC, magic) == 0)
		version = atoi(line + magic);
	if (version < 1 || version > STATE_VERSION) {
		fprintf(stderr, "%s: not a state file I know, "
			"starting fresh\n", state_path);
		fclose(f);
		return;
	}

	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;

		if (line[0] == '#') {
			read_setting(&lib->set, line + 1);
			continue;
		}

		/* Split on tabs, in place. */
		char *field[STATE_FIELDS];
		int n = 0;
		char *p = line;
		while (n < STATE_FIELDS) {
			field[n++] = p;
			p = strchr(p, '\t');
			if (!p)
				break;
			*p++ = 0;
		}
		/*
		 * Older versions stop sooner: version 1 before the
		 * percent, version 2 before the published date.
		 */
		if (n < STATE_FIELDS - 2)
			continue;

		mr_book *b = add_book(lib);
		b->path = dup_clean(field[0]);
		b->size = strtoll(field[1], NULL, 10);
		b->mtime = strtoll(field[2], NULL, 10);
		b->chapter = atoi(field[3]);
		b->page = atoi(field[4]);
		b->chapter_pages = atoi(field[5]);
		b->chapters = atoi(field[6]);
		b->last_read = strtoll(field[7], NULL, 10);
		b->title = dup_clean(field[8]);
		b->author = dup_clean(field[9]);
		if (n > 10)
			b->percent = atoi(field[10]);
		if (n > 11)
			b->published = dup_clean(field[11]);
	}
	fclose(f);
}

/*
 * Open the book just long enough to read its title and author. If
 * it has none, or won't open, use the file name without ".epub".
 */
static void read_metadata(fz_context *ctx, mr_book *b)
{
	char title[512] = "", author[512] = "", date[16];
	fz_document *doc = NULL;
	fz_var(doc);

	fz_try(ctx) {
		doc = fz_open_document(ctx, b->path);
		fz_lookup_metadata(ctx, doc, FZ_META_INFO_TITLE,
				   title, sizeof title);
		fz_lookup_metadata(ctx, doc, FZ_META_INFO_AUTHOR,
				   author, sizeof author);
	}
	fz_always(ctx)
		fz_drop_document(ctx, doc);
	fz_catch(ctx) {
		/* unreadable; fall through to the file name */
	}

	if (!title[0]) {
		const char *base = strrchr(b->path, '/');
		base = base ? base + 1 : b->path;
		snprintf(title, sizeof title, "%s", base);
		size_t n = strlen(title);
		if (n > 5)
			title[n - 5] = 0;
	}
	free(b->title);
	free(b->author);
	b->title = dup_clean(title);
	b->author = dup_clean(author);

	mr_epub_date(ctx, b->path, date, sizeof date);
	free(b->published);
	b->published = dup_clean(date);
}

static int is_epub(const char *name)
{
	size_t n = strlen(name);
	return n > 5 && strcasecmp(name + n - 5, ".epub") == 0;
}

static void scan_dir(fz_context *ctx, mr_library *lib, const char *dir,
		     int depth)
{
	if (depth > MAX_DEPTH)
		return;
	DIR *d = opendir(dir);
	if (!d)
		return;

	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		/* skips ".", "..", and hidden files like macOS's ._ */
		if (e->d_name[0] == '.')
			continue;

		char path[2048];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		/* the state file can't hold these characters */
		if (strpbrk(path, "\t\n\r"))
			continue;

		struct stat st;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			scan_dir(ctx, lib, path, depth + 1);
			continue;
		}
		if (!S_ISREG(st.st_mode) || !is_epub(e->d_name))
			continue;

		mr_book *b = find_book(lib, path);
		if (!b) {
			b = add_book(lib);
			b->path = dup_clean(path);
		}
		if (!b->title || b->size != (long long)st.st_size ||
		    b->mtime != (long long)st.st_mtime) {
			b->size = st.st_size;
			b->mtime = st.st_mtime;
			read_metadata(ctx, b);
		} else if (!b->published) {
			/* saved before dates were kept: just the date */
			char date[16];
			mr_epub_date(ctx, b->path, date, sizeof date);
			b->published = dup_clean(date);
		}
		b->seen = 1;
	}
	closedir(d);
}

void mr_library_scan(fz_context *ctx, mr_library *lib, const char *dir)
{
	for (int i = 0; i < lib->count; i++)
		lib->items[i].seen = 0;

	/* "books/" and "books" should find the same paths */
	char root[2048];
	snprintf(root, sizeof root, "%s", dir);
	size_t n = strlen(root);
	while (n > 1 && root[n - 1] == '/')
		root[--n] = 0;

	scan_dir(ctx, lib, root, 0);
}

/* "The Odyssey" files under O, the way Calibre and bookshops do it. */
static const char *sort_key(const char *title)
{
	static const char *articles[] = { "The ", "A ", "An " };
	for (size_t i = 0; i < sizeof articles / sizeof *articles; i++) {
		size_t n = strlen(articles[i]);
		if (strncasecmp(title, articles[i], n) == 0)
			return title + n;
	}
	return title;
}

/*
 * An author's last name, for sorting: the part before a comma
 * ("Ellis, Bret Easton"), else the last word ("Bret Easton Ellis").
 */
static void last_name(const char *author, char *buf, size_t n)
{
	const char *comma = strchr(author, ',');
	if (comma) {
		snprintf(buf, n, "%.*s", (int)(comma - author), author);
		return;
	}
	const char *sp = strrchr(author, ' ');
	snprintf(buf, n, "%s", sp ? sp + 1 : author);
}

/* qsort has no room for an argument, so the order rides along here. */
static int sort_how;

static int by_title(const mr_book *a, const mr_book *b)
{
	return strcasecmp(sort_key(a->title ? a->title : ""),
			  sort_key(b->title ? b->title : ""));
}

static int cmp_books(const void *pa, const void *pb)
{
	const mr_book *a = pa, *b = pb;
	if (a->seen != b->seen)
		return b->seen - a->seen;

	switch (sort_how) {
	case MR_SORT_TITLE:
		break;
	case MR_SORT_AUTHOR: {
		char la[128], lb[128];
		last_name(a->author ? a->author : "", la, sizeof la);
		last_name(b->author ? b->author : "", lb, sizeof lb);
		int c = strcasecmp(la, lb);
		if (c)
			return c;
		c = strcasecmp(a->author ? a->author : "",
			       b->author ? b->author : "");
		if (c)
			return c;
		break;
	}
	case MR_SORT_ADDED:
		if (a->mtime != b->mtime)
			return a->mtime < b->mtime ? 1 : -1;
		break;
	case MR_SORT_PUBLISHED: {
		const char *da = a->published ? a->published : "";
		const char *db = b->published ? b->published : "";
		if (!*da != !*db)	/* undated after dated */
			return !*da - !*db;
		int c = strcmp(db, da);	/* newest first */
		if (c)
			return c;
		break;
	}
	default:	/* MR_SORT_RECENT */
		if (a->last_read != b->last_read)
			return a->last_read < b->last_read ? 1 : -1;
		break;
	}
	return by_title(a, b);
}

void mr_library_sort(mr_library *lib)
{
	sort_how = lib->set.sort;
	qsort(lib->items, lib->count, sizeof(mr_book), cmp_books);
	lib->seen_count = 0;
	while (lib->seen_count < lib->count &&
	       lib->items[lib->seen_count].seen)
		lib->seen_count++;
}

/*
 * Write to a temporary file, push it all the way to the card with
 * fsync, then rename it over the old one. rename() replaces a name in
 * one step, so a dead battery mid-save leaves either the whole old
 * file or the whole new one, never half of each.
 */
int mr_library_save(const mr_library *lib)
{
	char tmp[2048];
	snprintf(tmp, sizeof tmp, "%s.tmp", lib->state_path);

	FILE *f = fopen(tmp, "w");
	if (!f) {
		perror(tmp);
		return -1;
	}
	fprintf(f, "%s%d\n", STATE_MAGIC, STATE_VERSION);
	for (size_t i = 0; i < N_SETTINGS; i++)
		fprintf(f, "#%s\t%d\n", setting_keys[i].key,
			*(const int *)((const char *)&lib->set +
				       setting_keys[i].offset));
	for (int i = 0; i < lib->count; i++) {
		const mr_book *b = &lib->items[i];
		fprintf(f, "%s\t%lld\t%lld\t%d\t%d\t%d\t%d\t%lld\t%s\t%s"
			"\t%d\t%s\n",
			b->path, b->size, b->mtime, b->chapter, b->page,
			b->chapter_pages, b->chapters, b->last_read,
			b->title ? b->title : "",
			b->author ? b->author : "", b->percent,
			b->published ? b->published : "");
	}
	if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
		perror(tmp);
		fclose(f);
		return -1;
	}
	fclose(f);
	if (rename(tmp, lib->state_path) != 0) {
		perror(lib->state_path);
		return -1;
	}
	return 0;
}

int mr_book_percent(const mr_book *b)
{
	if (b->chapter < 0)
		return -1;
	if (b->percent >= 0)
		return b->percent;
	if (b->chapters <= 0 || b->chapter_pages <= 0)
		return -1;
	/*
	 * A place saved before the percent was stored: count every
	 * chapter as the same length. Rough, but replaced as soon as
	 * the book is opened and the place saved again.
	 */
	double done = b->chapter + (b->page + 1.0) / b->chapter_pages;
	int pct = (int)(done * 100.0 / b->chapters);
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	return pct;
}

void mr_library_free(mr_library *lib)
{
	for (int i = 0; i < lib->count; i++) {
		free(lib->items[i].path);
		free(lib->items[i].title);
		free(lib->items[i].author);
		free(lib->items[i].published);
	}
	free(lib->items);
	memset(lib, 0, sizeof *lib);
}

int mr_library_recent(const mr_library *lib, mr_book **out, int n)
{
	int k = 0;
	for (int i = 0; i < lib->seen_count; i++) {
		mr_book *b = &lib->items[i];
		if (b->last_read <= 0)
			continue;
		/* insertion into a short list, newest first */
		int j = k < n ? k++ : n;
		while (j > 0 && out[j - 1]->last_read < b->last_read) {
			if (j < n)
				out[j] = out[j - 1];
			j--;
		}
		if (j < n)
			out[j] = b;
	}
	return k;
}
