
ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

TARGET_NAME = testgl

ifeq ($(platform), unix)
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=link.T -Wl,--no-undefined
   GL_LIB := -lGL
else ifeq ($(platform), pi)
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=link.T -Wl,--no-undefined
   CFLAGS += -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/vmcs_host/linux
   GLES := 1
   LIBS += -L/opt/vc/lib
else ifneq (,$(findstring armv,$(platform)))
   CC = gcc
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=link.T -Wl,--no-undefined
   CFLAGS += -I.
ifneq (,$(findstring gles,$(platform)))
   GLES := 1
else
   GL_LIB := -lGL
endif
ifneq (,$(findstring cortexa8,$(platform)))
   CFLAGS += -marm -mcpu=cortex-a8
else ifneq (,$(findstring cortexa9,$(platform)))
   CFLAGS += -marm -mcpu=cortex-a9
endif
   CFLAGS += -marm
ifneq (,$(findstring neon,$(platform)))
   CFLAGS += -mfpu=neon
endif
ifneq (,$(findstring softfloat,$(platform)))
   CFLAGS += -mfloat-abi=softfp
else ifneq (,$(findstring hardfloat,$(platform)))
   CFLAGS += -mfloat-abi=hard
endif
   CFLAGS += -DARM
else
   CC = gcc
   TARGET := $(TARGET_NAME)_libretro.dll
   SHARED := -shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=link.T -Wl,--no-undefined
   GL_LIB := -lopengl32
   CFLAGS += -I..
endif

ifeq ($(DEBUG), 1)
   CFLAGS += -O0 -g
else
   CFLAGS += -O3
endif

CFLAGS += -std=gnu99

OBJECTS := libretro-test.o ../gfx/glsym/rglgen.o
CFLAGS += -Wall -pedantic $(fpic)

ifeq ($(GLES), 1)
   CFLAGS += -DGLES -DHAVE_OPENGLES2
   ifeq ($(GLES31), 1)
      CFLAGS += -DHAVE_OPENGLES3 -DGLES31 -DGLES3
   else ifeq ($(GLES3), 1)
      CFLAGS += -DHAVE_OPENGLES3 -DGLES3
   endif
   LIBS += -lGLESv2 # Still link against GLESv2 when using GLES3 API, at least on desktop Linux.
   OBJECTS += ../gfx/glsym/glsym_es2.o
else
   OBJECTS += ../gfx/glsym/glsym_gl.o
   LIBS += $(GL_LIB)
endif

ifeq ($(CORE), 1)
   CFLAGS += -DCORE
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(fpic) $(SHARED) $(INCLUDES) -o $@ $(OBJECTS) $(LIBS) -lm $(EXTRA_GL_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean

