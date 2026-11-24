# The reader. Two builds from the same sources:
#
#   make        the Mac build: an SDL window, plus the --script backend
#   make nook   the Nook build: one static ARM binary that draws to the
#               framebuffer and reads the touchscreen. Needs Docker,
#               the GCC toolchain and MuPDF built for ARM (see the
#               README).

SRCS = reader.c ui.c home.c library.c settings.c book.c books.c \
       draw.c epub.c dict.c lookup.c system.c passcode.c \
       platform_script.c
HDRS = app.h platform.h books.h draw.h epub.h dict.h

# ---- Mac ----
CC = cc
CFLAGS = -Wall -Wextra -O2 -I/opt/homebrew/include \
	 $(shell sdl2-config --cflags)
LDFLAGS = -L/opt/homebrew/lib
LDLIBS = -lmupdf -lmupdf-third $(shell sdl2-config --libs)

reader: $(SRCS) platform_sdl.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) platform_sdl.c $(LDFLAGS) $(LDLIBS)

# ---- Nook ----
# GCC from Buildroot (scripts/build-toolchain.sh builds it). The
# toolchain is a Linux program, so "make nook" on the Mac starts the
# Docker container and runs "make reader-nook" inside it.
#
# Buildroot's gcc already targets the Cortex-A8 with NEON and hard
# float. musl is linked in (static), so the binary runs under Android
# (whose C library is different) and on bare Linux. -lstdc++ is for
# HarfBuzz, the one C++ library in MuPDF's build.
TOOLCHAIN = /br/buildroot/output/host/bin/arm-buildroot-linux-musleabihf-
NOOK_CC = $(TOOLCHAIN)gcc
MUPDF = ../vendor/mupdf-1.27.2-source
MUPDF_ARM = $(MUPDF)/build/nook-gcc-release-tofu_cjk
DOCKER = docker run --rm -v mynook-br:/br -v $(abspath ..):/src \
	 mynook-br

nook:
	$(DOCKER) make -C /src/reader reader-nook

# MuPDF for the Nook, built once (about a minute).
mupdf-nook:
	$(DOCKER) make -C /src/vendor/mupdf-1.27.2-source -j10 \
		OS=Linux build=release build_prefix=nook-gcc- \
		CC=$(TOOLCHAIN)gcc CXX=$(TOOLCHAIN)g++ AR=$(TOOLCHAIN)ar \
		HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no HAVE_LIBCRYPTO=no \
		HAVE_OBJCOPY=no mujs=no tofu_cjk=yes libs

reader-nook: $(SRCS) platform_fb.c $(HDRS)
	$(NOOK_CC) -Wall -Wextra -O2 -I$(MUPDF)/include \
		-DMR_DEFAULT_PLATFORM=mr_platform_fb -static -s \
		-o $@ $(SRCS) platform_fb.c \
		$(MUPDF_ARM)/libmupdf.a $(MUPDF_ARM)/libmupdf-third.a \
		-lstdc++ -lm

clean:
	rm -f reader reader-nook frame-*.ppm frame-*.png

.PHONY: nook mupdf-nook clean
