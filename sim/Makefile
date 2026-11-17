# XenithOS simulator: the real shell code, built for Linux with sim/platform_sim.c. Bluetooth is the
# PC's own code (linux/bluetooth_linux.c), run against a stand-in bluetoothctl (sim/bluetooth).
# Build: make -C sim        Run: sim/run.sh <script> <out-dir>   (see sim/README.md)

CC      ?= gcc
SHARED  := $(filter-out ../shared/platform_xenon.c ../shared/net_xenon.c, $(wildcard ../shared/*.c))
SRC     := $(wildcard ../source/*.c) $(SHARED) platform_sim.c net_sim.c ../linux/bluetooth_linux.c
HDRS    := $(wildcard ../shared/*.h ../source/*.h)
CFLAGS  := -O1 -g -std=gnu11 -Wall -Wno-format-truncation -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I../shared -I../source -DXENITH_SIM
LDLIBS  := -lz -lm

VOLT    := ../apps/volt/source/volt.c $(SHARED) platform_sim.c net_sim.c

# DOOM: doomgeneric's 1990s C trips the undefined-behaviour checker all over, so this one is
# checked for memory errors only.
DOOM    := $(wildcard ../apps/doom/doomgeneric/*.c) $(wildcard ../apps/doom/source/*.c) $(SHARED) platform_sim.c net_sim.c
DOOMFLAGS := -O1 -g -std=gnu11 -w -fsanitize=address -fno-omit-frame-pointer -I../apps/doom/doomgeneric -I../shared \
           -DXENITH_SIM -DFEATURE_SOUND -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200

TUBE    := $(wildcard ../apps/tube/source/*.c) $(SHARED) platform_sim.c net_sim.c
PHOTOS  := ../apps/photos/source/photos.c $(SHARED) platform_sim.c net_sim.c
HOSTMC  := $(wildcard ../apps/hostmc/source/*.c) $(SHARED) platform_sim.c net_sim.c

all: xsim xvolt xdoom xtube xphotos xhostmc

# stb_image is checked for memory errors only (it does its own careful casting).
xtube: $(TUBE) $(HDRS)
	$(CC) $(filter-out -fsanitize=address$(comma)undefined,$(CFLAGS)) -fsanitize=address -I../apps/tube/source $(TUBE) -o $@ $(LDLIBS)

comma := ,

# Photos decodes pictures with stb_image too: memory errors only, like Tube.
xphotos: $(PHOTOS) $(HDRS)
	$(CC) $(filter-out -fsanitize=address$(comma)undefined,$(CFLAGS)) -fsanitize=address $(PHOTOS) -o $@ $(LDLIBS)

# HostMC runs real programs (the server, curl, tar), so a script for it runs in real time
# (XSIM_THREAD=1); see sim/scripts/hostmc.txt.
xhostmc: $(HOSTMC) $(HDRS) $(wildcard ../apps/hostmc/source/*.h)
	$(CC) $(CFLAGS) -I../apps/hostmc/source $(HOSTMC) -o $@ $(LDLIBS)

xsim: $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDLIBS)

xvolt: $(VOLT) $(HDRS)
	$(CC) $(CFLAGS) $(VOLT) -o $@ $(LDLIBS)

xdoom: $(DOOM) $(HDRS)
	$(CC) $(DOOMFLAGS) $(DOOM) -o $@ $(LDLIBS)

clean:
	rm -f xsim xvolt xdoom xtube xphotos xhostmc
