/*
 * The dictionary: Wiktionary, prepared on the Mac by
 * scripts/dict/build_dict.py into dict.idx and dict.dat (the format
 * is described there). Both are mmap'd, so opening costs nothing and
 * the kernel reads in only the pages a lookup touches.
 */
#ifndef MR_DICT_H
#define MR_DICT_H

#include <stddef.h>

typedef struct mr_dict mr_dict;

/* Open dict.idx and dict.dat in dir. NULL if they're missing or bad. */
mr_dict *mr_dict_open(const char *dir);
void mr_dict_close(mr_dict *d);

/*
 * Look up a key (already made with mr_dict_key). On success returns
 * 1 and points *see at the base word to show too ("" if none) and
 * *html at the entry, html_len bytes, not NUL-terminated. The
 * pointers are into the mapped file: valid until mr_dict_close.
 */
int mr_dict_find(const mr_dict *d, const char *key, const char **see,
		 const char **html, size_t *html_len);

/*
 * A word as tapped (UTF-8, maybe with quotes or a comma stuck to it)
 * to a lookup key: curly apostrophes straightened, punctuation
 * trimmed from both ends, A-Z lowered. Returns the key's length, 0
 * if nothing word-like was left.
 */
size_t mr_dict_key(const char *word, char *key, size_t n);

/*
 * Find a word the way a reader means it: as typed, then without a
 * possessive ("dog's", "dogs'"). Fills key with the form found.
 */
int mr_dict_lookup(const mr_dict *d, const char *word, char *key,
		   size_t n, const char **see, const char **html,
		   size_t *html_len);

#endif
