# XenithOS for PCs and Raspberry Pis: the same code as the Xbox version, on Linux + SDL2.
#   make         builds xenithos and its apps here
#   make clean
# install.sh does the rest (packages, installing, console mode). Needs libsdl2-dev and zlib.
# HTTPS uses the system's OpenSSL (libssl.so.3), opened when it's needed (http_posix.c), so
# building doesn't need libssl-dev.

CC          ?= cc
SDL_CFLAGS  := $(shell sdl2-config --cflags)
SDL_LIBS    := $(shell sdl2-config --libs)
OPT         ?= -O2 -g
# Crash files (crash_linux.c): unwind tables so backtrace() can walk the stack on 32-bit Pis too,
# and a build id, so a report names the very program it came from.
CFLAGS_ALL  := $(OPT) -std=gnu11 -Wall -Wno-format-truncation -funwind-tables -I../shared -I../source $(SDL_CFLAGS) -DXENITH_LINUX
LIBS        := $(SDL_LIBS) -lX11 -lz -lm -lpthread -ldl -Wl,--build-id

SHARED      := $(filter-out ../shared/platform_xenon.c ../shared/net_xenon.c, $(wildcard ../shared/*.c))
PLATFORM    := platform_sdl.c apps_linux.c kiosk.c net_posix.c http_posix.c crash_linux.c bluetooth_linux.c
HEADERS     := $(wildcard ../shared/*.h ../source/*.h) platform_linux.h http_posix.h

SHELL_SRC   := $(wildcard ../source/*.c) $(SHARED) $(PLATFORM)
VOLT_SRC    := ../apps/volt/source/volt.c $(SHARED) $(PLATFORM)
DOOM_SRC    := $(wildcard ../apps/doom/doomgeneric/*.c) $(wildcard ../apps/doom/source/*.c) $(SHARED) $(PLATFORM)
DOOM_CFLAGS := $(OPT) -std=gnu11 -w -funwind-tables -I../apps/doom/doomgeneric -I../shared $(SDL_CFLAGS) -DXENITH_LINUX \
               -DFEATURE_SOUND -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200

TUBE_SRC    := $(wildcard ../apps/tube/source/*.c) $(SHARED) $(PLATFORM)
PHOTOS_SRC  := ../apps/photos/source/photos.c $(SHARED) $(PLATFORM)
MINENEST_SRC := $(wildcard ../apps/minenest/source/*.c) $(SHARED) $(PLATFORM)
MUSIC_SRC   := $(wildcard ../apps/music/source/*.c) $(SHARED) $(PLATFORM)

all: xenithos volt doom tube photos minenest music

# Music decodes with dr_mp3, dr_flac and stb_vorbis (shared/), in apps/music/source/decode.c.
music: $(MUSIC_SRC) $(HEADERS) $(wildcard ../apps/music/source/*.h)
	$(CC) $(CFLAGS_ALL) -I../apps/music/source $(MUSIC_SRC) -o $@ $(LIBS)

tube: $(TUBE_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) -I../apps/tube/source $(TUBE_SRC) -o $@ $(LIBS)

photos: $(PHOTOS_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) $(PHOTOS_SRC) -o $@ $(LIBS)

# MineNest is for PCs only (Minecraft servers), so it has no Xbox build. The servers are run by
# the minenest program (github.com/OwenCosta/minenest), which get-minenest.sh puts beside it.
minenest: $(MINENEST_SRC) $(HEADERS) $(wildcard ../apps/minenest/source/*.h)
	$(CC) $(CFLAGS_ALL) -I../apps/minenest/source $(MINENEST_SRC) -o $@ $(LIBS)

xenithos: $(SHELL_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) $(SHELL_SRC) -o $@ $(LIBS)

volt: $(VOLT_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) $(VOLT_SRC) -o $@ $(LIBS)

doom: $(DOOM_SRC) $(HEADERS)
	$(CC) $(DOOM_CFLAGS) $(DOOM_SRC) -o $@ $(LIBS)

clean:
	rm -f xenithos volt doom tube photos minenest music

.PHONY: all clean
