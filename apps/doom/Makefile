#---------------------------------------------------------------------------------
# DOOM (XENITH app, doomgeneric) - LibXenon build (based on the devkitxenon example Makefile)
#   make        -> xenith.elf and xenith.elf32
#   make clean
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITXENON)),)
$(error "Please set DEVKITXENON in your environment, e.g. export DEVKITXENON=$$HOME/xenon")
endif

include $(DEVKITXENON)/rules

TARGET		:=	doom
BUILD		:=	build
SOURCES		:=	source doomgeneric ../../shared
DATA		:=
INCLUDES	:=	doomgeneric ../../shared

CFLAGS		=	-g -O2 -Wall -Wno-unused -DFEATURE_SOUND -Wno-pointer-sign -Wno-format-truncation -Wno-stringop-truncation -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	-g $(MACHDEP) -Wl,--gc-sections -Wl,-Map,$(notdir $@).map

LIBS		:=	-lfat -lz -lxenon -lm
LIBDIRS		:=

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD	:=	$(CC)
export OFILES	:=	$(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			-I$(CURDIR)/$(BUILD) \
			-I$(LIBXENON_INC)
export LIBPATHS	:=	-L$(LIBXENON_LIB)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).elf32

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).elf32: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
