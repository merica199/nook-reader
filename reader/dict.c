/*
 * Dictionary lookups straight from the mapped files. See dict.h.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "dict.h"

#define MAGIC "MRDICT1\n"
#define HEAD 12		/* magic (8) + count (4) */

struct mr_dict {
	const unsigned char *idx, *dat;
	size_t idx_size, dat_size;
	uint32_t count;
};

/* A whole file, read-only. NULL (and *size 0) if it can't be. */
static const unsigned char *map_file(const char *path, size_t *size)
{
	*size = 0;
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	struct stat st;
	void *p = MAP_FAILED;
	if (fstat(fd, &st) == 0 && st.st_size > 0)
		p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE,
			 fd, 0);
	close(fd);	/* the mapping stays after the file is closed */
	if (p == MAP_FAILED)
		return NULL;
	*size = (size_t)st.st_size;
	return p;
}

/* Little-endian uint32 at p, whatever p's alignment. */
static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void mr_dict_close(mr_dict *d)
{
	if (!d)
		return;
	if (d->idx)
		munmap((void *)d->idx, d->idx_size);
	if (d->dat)
		munmap((void *)d->dat, d->dat_size);
	free(d);
}

mr_dict *mr_dict_open(const char *dir)
{
	char path[1024];
	mr_dict *d = calloc(1, sizeof *d);
	if (!d)
		return NULL;

	snprintf(path, sizeof path, "%s/dict.idx", dir);
	d->idx = map_file(path, &d->idx_size);
	snprintf(path, sizeof path, "%s/dict.dat", dir);
	d->dat = map_file(path, &d->dat_size);
	if (!d->idx || !d->dat || d->idx_size < HEAD ||
	    memcmp(d->idx, MAGIC, 8) != 0)
		goto bad;

	/* the index must be exactly as long as its count says */
	d->count = le32(d->idx + 8);
	if ((d->idx_size - HEAD) / 8 != d->count ||
	    (d->idx_size - HEAD) % 8 != 0)
		goto bad;
	return d;
bad:
	mr_dict_close(d);
	return NULL;
}

/*
 * Record i: key NUL see NUL html. Returns 0 if the record runs off
 * the end of the file or is missing its NULs (a damaged file).
 */
static int record(const mr_dict *d, uint32_t i, const char **key,
		  const char **see, const char **html, size_t *html_len)
{
	const unsigned char *e = d->idx + HEAD + (size_t)i * 8;
	uint32_t off = le32(e), len = le32(e + 4);
	if (off > d->dat_size || len > d->dat_size - off)
		return 0;
	const char *r = (const char *)d->dat + off, *end = r + len;
	const char *nul1 = memchr(r, '\0', len);
	if (!nul1)
		return 0;
	const char *nul2 = memchr(nul1 + 1, '\0', end - (nul1 + 1));
	if (!nul2)
		return 0;
	*key = r;
	*see = nul1 + 1;
	*html = nul2 + 1;
	*html_len = end - (nul2 + 1);
	return 1;
}

int mr_dict_find(const mr_dict *d, const char *key, const char **see,
		 const char **html, size_t *html_len)
{
	if (!d || !*key)
		return 0;
	/* keys are sorted by bytes, which is what strcmp compares */
	uint32_t lo = 0, hi = d->count;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		const char *k;
		if (!record(d, mid, &k, see, html, html_len))
			return 0;
		int c = strcmp(key, k);
		if (c == 0)
			return 1;
		if (c < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return 0;
}

/*
 * Punctuation that sticks to words in running text, as UTF-8: curly
 * quotes, dashes, the ellipsis, guillemets.
 */
static const char *const sticky[] = {
	"\xe2\x80\x9c", "\xe2\x80\x9d", "\xe2\x80\x98", "\xe2\x80\x99",
	"\xe2\x80\x93", "\xe2\x80\x94", "\xe2\x80\xa6", "\xc2\xab",
	"\xc2\xbb",
};

/* Length of sticky punctuation starting at s, 0 if none. */
static size_t sticky_at(const char *s)
{
	for (size_t i = 0; i < sizeof sticky / sizeof *sticky; i++) {
		size_t n = strlen(sticky[i]);
		if (strncmp(s, sticky[i], n) == 0)
			return n;
	}
	return 0;
}

/* Sticky punctuation that ends exactly at end, 0 if none. */
static size_t sticky_before(const char *start, const char *end)
{
	for (size_t i = 0; i < sizeof sticky / sizeof *sticky; i++) {
		size_t n = strlen(sticky[i]);
		if ((size_t)(end - start) >= n &&
		    memcmp(end - n, sticky[i], n) == 0)
			return n;
	}
	return 0;
}

/* ASCII punctuation never at either end of a word. */
static int ascii_trim(unsigned char c)
{
	return c < 0x80 && !(c >= 'a' && c <= 'z') &&
	       !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9');
}

size_t mr_dict_key(const char *word, char *key, size_t n)
{
	const char *s = word, *e = word + strlen(word);
	size_t k;

	/* trim the ends */
	for (;;) {
		if (s < e && ascii_trim((unsigned char)*s))
			s++;
		else if ((k = sticky_at(s)) && s + k <= e)
			s += k;
		else
			break;
	}
	for (;;) {
		if (e > s && ascii_trim((unsigned char)e[-1]))
			e--;
		else if ((k = sticky_before(s, e)))
			e -= k;
		else
			break;
	}

	/* copy, lowering A-Z and straightening the curly apostrophe */
	size_t len = 0;
	while (s < e && len + 1 < n) {
		if (strncmp(s, "\xe2\x80\x99", 3) == 0) {
			key[len++] = '\'';
			s += 3;
			continue;
		}
		char c = *s++;
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		key[len++] = c;
	}
	key[len] = '\0';
	return len;
}

int mr_dict_lookup(const mr_dict *d, const char *word, char *key,
		   size_t n, const char **see, const char **html,
		   size_t *html_len)
{
	size_t len = mr_dict_key(word, key, n);
	if (len == 0)
		return 0;
	if (mr_dict_find(d, key, see, html, html_len))
		return 1;

	/* possessives: "dog's" and "dogs'" */
	if (len > 2 && strcmp(key + len - 2, "'s") == 0) {
		key[len - 2] = '\0';
		if (mr_dict_find(d, key, see, html, html_len))
			return 1;
		key[len - 2] = '\'';
	}
	if (len > 1 && key[len - 1] == '\'') {
		key[len - 1] = '\0';
		if (mr_dict_find(d, key, see, html, html_len))
			return 1;
		key[len - 1] = '\'';
	}
	return 0;
}
