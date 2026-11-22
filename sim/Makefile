# XenithOS simulator: the real shell code, built for Linux with sim/platform_sim.c. Bluetooth is the
# PC's own code (linux/bluetooth_linux.c), run against a stand-in bluetoothctl (sim/bluetooth).
# Build: make -C sim        Run: sim/run.sh <script> <out-dir>   (see sim/README.md)

CC      ?= gcc
SHARED  := $(filter-out ../shared/platform_xenon.c ../shared/net_xenon.c, $(wildcard ../shared/*.c))
# The network is the PC's own client (../linux/http_posix.c, HTTPS included) behind net_sim.c.
NET     := net_sim.c ../linux/http_posix.c
SRC     := $(wildcard ../source/*.c) $(SHARED) platform_sim.c $(NET) ../linux/bluetooth_linux.c
HDRS    := $(wildcard ../shared/*.h ../source/*.h) ../linux/http_posix.h
CFLAGS  := -O1 -g -std=gnu11 -Wall -Wno-format-truncation -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I../shared -I../source -I../linux -DXENITH_SIM
LDLIBS  := -lz -lm -ldl

VOLT    := ../apps/volt/source/volt.c $(SHARED) platform_sim.c $(NET)

# DOOM: doomgeneric's 1990s C trips the undefined-behaviour checker all over, so this one is
# checked for memory errors only.
DOOM    := $(wildcard ../apps/doom/doomgeneric/*.c) $(wildcard ../apps/doom/source/*.c) $(SHARED) platform_sim.c $(NET)
DOOMFLAGS := -O1 -g -std=gnu11 -w -fsanitize=address -fno-omit-frame-pointer -I../apps/doom/doomgeneric -I../shared \
           -I../linux -DXENITH_SIM -DFEATURE_SOUND -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200

TUBE    := $(wildcard ../apps/tube/source/*.c) $(SHARED) platform_sim.c $(NET)
PHOTOS  := ../apps/photos/source/photos.c $(SHARED) platform_sim.c $(NET)
MINENEST := $(wildcard ../apps/minenest/source/*.c) $(SHARED) platform_sim.c $(NET)
MUSIC   := $(wildcard ../apps/music/source/*.c) $(SHARED) platform_sim.c $(NET)

all: xsim xvolt xdoom xtube xphotos xminenest xmusic

# Music: its decoders (dr_mp3, dr_flac, stb_vorbis) and stb_image for covers do their own careful
# casting, so like Photos it's checked for memory errors only. MUSIC_FLAGS adds more (-I...).
xmusic: $(MUSIC) $(HDRS) $(wildcard ../apps/music/source/*.h)
	$(CC) $(filter-out -fsanitize=address$(comma)undefined,$(CFLAGS)) -fsanitize=address -I../apps/music/source $(MUSIC_FLAGS) $(MUSIC) -o $@ $(LDLIBS)

# stb_image is checked for memory errors only (it does its own careful casting).
xtube: $(TUBE) $(HDRS)
	$(CC) $(filter-out -fsanitize=address$(comma)undefined,$(CFLAGS)) -fsanitize=address -I../apps/tube/source $(TUBE) -o $@ $(LDLIBS)

comma := ,

# Photos decodes pictures with stb_image too: memory errors only, like Tube.
xphotos: $(PHOTOS) $(HDRS)
	$(CC) $(filter-out -fsanitize=address$(comma)undefined,$(CFLAGS)) -fsanitize=address $(PHOTOS) -o $@ $(LDLIBS)

# MineNest talks to the minenest program, which runs real servers, so a script for it runs in
# real time (XSIM_THREAD=1); see sim/scripts/minenest.txt.
xminenest: $(MINENEST) $(HDRS) $(wildcard ../apps/minenest/source/*.h)
	$(CC) $(CFLAGS) -I../apps/minenest/source $(MINENEST) -o $@ $(LDLIBS)

xsim: $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDLIBS)

xvolt: $(VOLT) $(HDRS)
	$(CC) $(CFLAGS) $(VOLT) -o $@ $(LDLIBS)

xdoom: $(DOOM) $(HDRS)
	$(CC) $(DOOMFLAGS) $(DOOM) -o $@ $(LDLIBS)

clean:
	rm -f xsim xvolt xdoom xtube xphotos xminenest xmusic
