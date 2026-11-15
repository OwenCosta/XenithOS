# XenithOS simulator: the real shell code, built for Linux with sim/platform_sim.c.
# Build: make -C sim        Run: sim/run.sh <script> <out-dir>   (see sim/README.md)

CC      ?= gcc
SHARED  := $(filter-out ../shared/platform_xenon.c ../shared/net_xenon.c, $(wildcard ../shared/*.c))
SRC     := $(wildcard ../source/*.c) $(SHARED) platform_sim.c net_sim.c
HDRS    := $(wildcard ../shared/*.h ../source/*.h)
CFLAGS  := -O1 -g -std=gnu11 -Wall -Wno-format-truncation -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I../shared -I../source -DXENITH_SIM
LDLIBS  := -lz -lm

VOLT    := ../apps/volt/source/volt.c $(SHARED) platform_sim.c net_sim.c

all: xsim xvolt

xsim: $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDLIBS)

xvolt: $(VOLT) $(HDRS)
	$(CC) $(CFLAGS) $(VOLT) -o $@ $(LDLIBS)

clean:
	rm -f xsim xvolt
