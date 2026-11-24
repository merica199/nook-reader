"""
Build MyReader's dictionary from kaikki.org's English Wiktionary.

    python3 -I scripts/dict/build_dict.py \\
        vendor/wiktionary/kaikki.org-dictionary-English.jsonl dict/

Writes two files the reader opens with mmap and never loads whole:

  dict.dat  one record per lookup key, back to back:
            key NUL see NUL html
              key   the word, A-Z lowered ("polish" holds both
                    "polish" and "Polish")
              see   a base word to show under this one ("run" for
                    "ran"), or empty
              html  the entry, ready for MuPDF's HTML layout

  dict.idx  "MRDICT1\\n", a little-endian uint32 count, then count
            pairs of uint32 (offset, length) into dict.dat, sorted by
            key bytes, so the reader finds a word by binary search
            with strcmp.

Source: Wiktionary (CC BY-SA), via kaikki.org's wiktextract.
"""
import html
import json
import os
import struct
import sys
from collections import defaultdict

# Run with python3 -I (isolated: the current folder isn't searched),
# so add this script's own folder for respell.py by hand.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from respell import respell  # noqa: E402

MAX_SENSES = 8	# per part of speech; "set" has dozens

POS_NAMES = {
    "adj": "adjective", "adv": "adverb", "name": "proper noun",
    "prep": "preposition", "conj": "conjunction", "pron": "pronoun",
    "intj": "interjection", "det": "determiner", "num": "numeral",
    "abbrev": "abbreviation", "prep_phrase": "prepositional phrase",
    "article": "article", "contraction": "contraction",
}
AMERICAN = {"US", "General-American"}


def lookup_key(word):
    """Lower A-Z only. The reader does exactly the same in C."""
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c
                   for c in word)


def pick_ipa(sounds):
    """The American pronunciation if there is one, else the first."""
    first = None
    for s in sounds:
        ipa = s.get("ipa")
        if not ipa or not ipa.startswith("/"):
            continue	# [phonetic] forms are too fine-grained
        if AMERICAN & set(s.get("tags", [])):
            return ipa
        if first is None:
            first = ipa
    return first


def senses_of(entry):
    """The definitions worth showing, and the first base word."""
    out, base = [], None
    for s in entry.get("senses", []):
        if "no-gloss" in s.get("tags", []):
            continue
        text = (s.get("raw_glosses") or s.get("glosses") or [None])[-1]
        if not text or text in out:
            continue
        if base is None:
            for link in s.get("form_of", []) + s.get("alt_of", []):
                if link.get("word"):
                    base = link["word"]
                    break
        if len(out) < MAX_SENSES:
            out.append(text)
    return out, base


def main(src, out_dir):
    # key -> list of (sort order, word, pos, respelling, senses)
    entries = defaultdict(list)
    bases = {}
    n = 0
    with open(src, encoding="utf-8") as f:
        for line in f:
            e = json.loads(line)
            word = e.get("word", "")
            if not word or " " in word:
                continue	# phrases can't be tapped as one word
            senses, base = senses_of(e)
            if not senses:
                continue
            ipa = pick_ipa(e.get("sounds", []))
            spoken = respell(ipa) if ipa else ""
            key = lookup_key(word)
            # lowercase headwords first: "polish" before "Polish"
            order = (word != key, n)
            pos = POS_NAMES.get(e.get("pos"), e.get("pos", ""))
            entries[key].append((order, word, pos, spoken, senses))
            if base and key not in bases and lookup_key(base) != key:
                bases[key] = lookup_key(base)
            n += 1
            if n % 200000 == 0:
                print(f"{n} entries", file=sys.stderr)

    keys = sorted(entries, key=lambda k: k.encode("utf-8"))
    idx, offset = [], 0
    with open(f"{out_dir}/dict.dat", "wb") as dat:
        for key in keys:
            parts = []
            last = None
            for _, word, pos, spoken, senses in sorted(entries[key]):
                head = (word, spoken)
                if head != last:	# same word and sound: one heading
                    pr = f' <span class="pr">{html.escape(spoken)}</span>' \
                        if spoken else ""
                    parts.append(f'<p class="w"><b>{html.escape(word)}'
                                 f'</b>{pr}</p>')
                    last = head
                items = "".join(f"<li>{html.escape(s)}</li>"
                                for s in senses)
                parts.append(f'<p class="pos">{html.escape(pos)}</p>'
                             f"<ol>{items}</ol>")
            rec = (key.encode() + b"\0" +
                   bases.get(key, "").encode() + b"\0" +
                   "".join(parts).encode())
            dat.write(rec)
            idx.append((offset, len(rec)))
            offset += len(rec)
    if offset >= 2 ** 32:
        sys.exit("dict.dat is over 4 GB; offsets need 64 bits")

    with open(f"{out_dir}/dict.idx", "wb") as f:
        f.write(b"MRDICT1\n")
        f.write(struct.pack("<I", len(idx)))
        for off, length in idx:
            f.write(struct.pack("<II", off, length))
    print(f"{n} entries, {len(keys)} keys, {offset} bytes of entries",
          file=sys.stderr)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: build_dict.py <kaikki .jsonl> <output folder>")
    main(sys.argv[1], sys.argv[2])
