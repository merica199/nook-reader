#!/bin/sh
# Download what the build needs into vendor/, checking each against
# the SHA-256 recorded when it was first fetched.
#
#   scripts/fetch.sh          MuPDF and Buildroot (about 75 MB)
#   scripts/fetch.sh dict     also Wiktionary for the dictionary (3.4 GB)
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
V="$ROOT/vendor"
mkdir -p "$V"

# fetch <url> <file> <sha256>
fetch() {
	if [ ! -f "$V/$2" ]; then
		echo "Downloading $2..."
		curl -sSfL -o "$V/$2.part" "$1"
		mv "$V/$2.part" "$V/$2"
	fi
	got=$(shasum -a 256 "$V/$2" | cut -d' ' -f1)
	if [ "$got" != "$3" ]; then
		echo "$2: SHA-256 is $got, expected $3" >&2
		exit 1
	fi
	echo "$2: checked"
}

# MuPDF 1.27.2: the hash matches what Homebrew recorded for the same
# release, so the Mac build and the Nook build run the same engine.
fetch https://mupdf.com/downloads/archive/mupdf-1.27.2-source.tar.gz \
	mupdf-1.27.2-source.tar.gz \
	553867b135303dc4c25ab67c5f234d8e900a0e36e66e8484d99adc05fe1e8737
if [ ! -d "$V/mupdf-1.27.2-source" ]; then
	tar -C "$V" -xzf "$V/mupdf-1.27.2-source.tar.gz"
fi

# Buildroot 2026.08.2: the hash is the one in Buildroot's .sign file.
fetch https://buildroot.org/downloads/buildroot-2026.08.2.tar.xz \
	buildroot-2026.08.2.tar.xz \
	bc9497c8e3a734c277e541e2f7fcf9a6275dc92f75bd2f753b05981c54be8705

# Wiktionary changes every few weeks at the same address, so there's
# no fixed hash to check; the dictionary is rebuilt from whatever
# version is here.
if [ "$1" = dict ]; then
	mkdir -p "$V/wiktionary"
	f="$V/wiktionary/kaikki.org-dictionary-English.jsonl"
	if [ ! -f "$f" ]; then
		echo "Downloading Wiktionary (3.4 GB)..."
		curl -sSfL -C - -o "$f.part" \
			https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl
		mv "$f.part" "$f"
	fi
	echo "Wiktionary: present"
fi
