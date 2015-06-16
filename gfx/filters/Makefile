compiler    := gcc
extra_flags :=
use_neon    := 0
release	    := release
neon_asm    :=
DYLIB	    := so

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

ifeq ($(platform), unix)
extra_rules_gcc := $(shell $(compiler) -dumpmachine)
endif

ifneq (,$(findstring armv7,$(extra_rules_gcc)))
extra_flags += -mcpu=cortex-a9 -mtune=cortex-a9 -mfpu=neon
use_neon := 1
endif

ifneq (,$(findstring hardfloat,$(extra_rules_gcc)))
extra_flags += -mfloat-abi=hard
endif

ifeq ($(build), release)
extra_flags += -O2
endif

ifeq ($(build), debug)
extra_flags += -O0 -g
endif

ldflags := -shared -Wl,--version-script=link.T

ifeq ($(platform), unix)
DYLIB = so
else
extra_flags += -static-libgcc -static-libstdc++
DYLIB = dll
endif

flags   := -fPIC $(extra_flags)
asflags := -fPIC  $(extra_flags)
objects :=
flags   += -std=c99


ifeq ($(use_neon), 1)
asflags += -mfpu=neon -Iasm
flags += -DUSE_NEON
neon_asm := $(patsubst %.S,%.o,$(wildcard *_neon.S))
endif

objects += blargg_ntsc_snes_composite.$(DYLIB) blargg_ntsc_snes_rf.$(DYLIB) blargg_ntsc_snes_svideo.$(DYLIB) blargg_ntsc_snes_rgb.$(DYLIB) phosphor2x.$(DYLIB) epx.$(DYLIB) lq2x.$(DYLIB) 2xsai.$(DYLIB) super2xsai.$(DYLIB) supereagle.$(DYLIB) 2xbr.$(DYLIB) darken.$(DYLIB) scale2x.$(DYLIB)

all: build;

%.o: %.S
	$(compiler) -c -o $@ $(asflags) $<

%.o: %.c
	$(compiler) -c -o $@ $(flags) $<

.SECONDEXPANSION:
%.$(DYLIB): %.o $$(findstring %_neon.o,$$(neon_asm))
	$(compiler) -o $@ $(ldflags) $(flags) $^

build: $(objects)

clean:
	rm -f *.o
	rm -f *.$(DYLIB)

strip:
	strip -s *.$(DYLIB)
