# nook-reader: the reader and MyNook, the system it runs on.
#
#   make fetch      download MuPDF and Buildroot (checked by SHA-256)
#   make reader     the Mac build, to try the reader in a window
#   make toolchain  GCC for the Nook, built by Buildroot in Docker
#   make mupdf      MuPDF for the Nook (once)
#   make nook       the reader for the Nook
#   make os         MyNook: os/out/uAltRam
#   make dict       the dictionary, from Wiktionary (3.4 GB download)
#   make install    put MyNook on the Nook, over adb
#   make run        try the reader on the Nook under Android
#
# From nothing: make fetch toolchain mupdf nook os dict install.
# See README.md and docs/.

fetch:
	scripts/fetch.sh

reader:
	$(MAKE) -C reader

toolchain:
	scripts/build-toolchain.sh

mupdf:
	$(MAKE) -C reader mupdf-nook

nook:
	$(MAKE) -C reader nook

os: nook
	scripts/build-os.sh

dict:
	scripts/fetch.sh dict
	mkdir -p dict
	python3 -I scripts/dict/build_dict.py \
		vendor/wiktionary/kaikki.org-dictionary-English.jsonl dict

install:
	scripts/nook-install.sh

run:
	scripts/nook-run.sh

.PHONY: fetch reader toolchain mupdf nook os dict install run
