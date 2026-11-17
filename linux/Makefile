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
PLATFORM    := platform_sdl.c apps_linux.c kiosk.c net_posix.c http_posix.c crash_linux.c
HEADERS     := $(wildcard ../shared/*.h ../source/*.h) platform_linux.h http_posix.h

SHELL_SRC   := $(wildcard ../source/*.c) $(SHARED) $(PLATFORM)
VOLT_SRC    := ../apps/volt/source/volt.c $(SHARED) $(PLATFORM)
DOOM_SRC    := $(wildcard ../apps/doom/doomgeneric/*.c) $(wildcard ../apps/doom/source/*.c) $(SHARED) $(PLATFORM)
DOOM_CFLAGS := $(OPT) -std=gnu11 -w -funwind-tables -I../apps/doom/doomgeneric -I../shared $(SDL_CFLAGS) -DXENITH_LINUX \
               -DFEATURE_SOUND -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200

TUBE_SRC    := $(wildcard ../apps/tube/source/*.c) $(SHARED) $(PLATFORM)
PHOTOS_SRC  := ../apps/photos/source/photos.c $(SHARED) $(PLATFORM)
HOSTMC_SRC  := $(wildcard ../apps/hostmc/source/*.c) $(SHARED) $(PLATFORM)

all: xenithos volt doom tube photos hostmc

tube: $(TUBE_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) -I../apps/tube/source $(TUBE_SRC) -o $@ $(LIBS)

photos: $(PHOTOS_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) $(PHOTOS_SRC) -o $@ $(LIBS)

# HostMC is for PCs only (a Minecraft server), so it has no Xbox build.
hostmc: $(HOSTMC_SRC) $(HEADERS) $(wildcard ../apps/hostmc/source/*.h)
	$(CC) $(CFLAGS_ALL) -I../apps/hostmc/source $(HOSTMC_SRC) -o $@ $(LIBS)

xenithos: $(SHELL_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) $(SHELL_SRC) -o $@ $(LIBS)

volt: $(VOLT_SRC) $(HEADERS)
	$(CC) $(CFLAGS_ALL) $(VOLT_SRC) -o $@ $(LIBS)

doom: $(DOOM_SRC) $(HEADERS)
	$(CC) $(DOOM_CFLAGS) $(DOOM_SRC) -o $@ $(LIBS)

clean:
	rm -f xenithos volt doom tube photos hostmc

.PHONY: all clean
