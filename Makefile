CC = gcc
CFLAGS = -std=c99 -O2 -g
LDFLAGS = 

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -I/opt/homebrew/include
    LDFLAGS += -L/opt/homebrew/lib
    LDFLAGS += -framework CoreFoundation -framework IOKit
endif
ifeq ($(UNAME_S),Linux)
    CFLAGS += -DLINUX
    LDFLAGS += -lpthread -lm
endif
ifeq ($(UNAME_S),FreeBSD)
    LDFLAGS += -lpthread -lm
endif
ifeq ($(UNAME_S),NetBSD)
    LDFLAGS += -lpthread -lm
endif
ifeq ($(UNAME_S),OpenBSD)
    LDFLAGS += -lpthread -lm
endif

# Graphics driver selection
ifeq ($(GFX),SDL3)
    CFLAGS += -DGFX_SDL3 $(shell pkg-config --cflags sdl3 sdl3-ttf fontconfig)
    LDFLAGS += $(shell pkg-config --libs sdl3 sdl3-ttf fontconfig)
endif
ifeq ($(GFX),SDL2)
    CFLAGS += -DGFX_SDL2 $(shell pkg-config --cflags fontconfig)
    LDFLAGS += -lSDL2 -lSDL2_ttf $(shell pkg-config --libs fontconfig)
endif
ifeq ($(GFX),X11)
    CFLAGS += -DGFX_X11
    LDFLAGS += -lX11
endif
ifeq ($(GFX),GTK3)
    CFLAGS += -DGFX_GTK3 $(shell pkg-config --cflags gtk+-3.0)
    LDFLAGS += $(shell pkg-config --libs gtk+-3.0)
endif
ifeq ($(GFX),GLFW)
    CFLAGS += -DGFX_GLFW $(shell pkg-config --cflags glfw3 freetype2)
    ifeq ($(UNAME_S),Darwin)
        LDFLAGS += $(shell pkg-config --libs glfw3 freetype2) -framework Cocoa -framework OpenGL -framework IOKit
    else
        LDFLAGS += $(shell pkg-config --libs glfw3 freetype2) -lGL
    endif
endif

# Default to GTK3 if no graphics driver specified
ifeq ($(GFX),)
    CFLAGS += -DGFX_GTK3 $(shell pkg-config --cflags gtk+-3.0)
    LDFLAGS += $(shell pkg-config --libs gtk+-3.0)
endif

SOURCES = main.c platform.c graphics.c config.c plot.c ringbuf.c threading.c ini_parser.c datasource.c ds/ping.c ds/sryze-ping.c ds/cpu.c ds/memory.c ds/sine.c ds/snmp.c ds/if_thr.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = plottool

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install
