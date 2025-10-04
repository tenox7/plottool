CC = gcc
CFLAGS = -O2
LDFLAGS = 

# Platform detection
UNAME_S ?= $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -I/opt/homebrew/include -I/opt/homebrew/opt/net-snmp/include -I/opt/homebrew/opt/openssl/include
    LDFLAGS += -L/opt/homebrew/lib -L/opt/homebrew/opt/net-snmp/lib -L/opt/homebrew/opt/openssl/lib
    LDFLAGS += -framework CoreFoundation -framework IOKit -lnetsnmp
endif
ifeq ($(UNAME_S),Linux)
    CFLAGS += -DLINUX
    LDFLAGS += -lpthread -lm
    # Try to add Net-SNMP support if available
    ifneq ($(shell which net-snmp-config),)
        CFLAGS += $(shell net-snmp-config --cflags 2>/dev/null)
        LDFLAGS += $(shell net-snmp-config --libs 2>/dev/null)
    endif
endif
ifeq ($(UNAME_S),FreeBSD)
    LDFLAGS += -lpthread -lm -lnetsnmp
endif
ifeq ($(UNAME_S),NetBSD)
    LDFLAGS += -lpthread -lm -lnetsnmp
endif
ifeq ($(UNAME_S),OpenBSD)
    LDFLAGS += -lpthread -lm -lnetsnmp
endif
ifeq ($(UNAME_S),SunOS)
    LDFLAGS += -lpthread -lm -lsocket -lnsl -lrt -lkstat -lnetsnmp
endif
ifeq ($(UNAME_S),HP-UX)
    CFLAGS += -I/usr/local/include
    LDFLAGS += -L/usr/local/lib -lpthread -lm -lnetsnmp
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

# Default to X11 if no graphics driver specified
ifeq ($(GFX),)
    CFLAGS += -DGFX_X11
    LDFLAGS += -lX11
endif

ifeq ($(UNAME_S),SunOS)
    PING_SRC = ds/unix-ping.c
else
    ifeq ($(UNAME_S),HP-UX)
        PING_SRC = ds/unix-ping.c
    else
        PING_SRC = ds/sryze-ping.c
    endif
endif

SOURCES = main.c platform.c graphics.c config.c plot.c ringbuf.c threading.c ini_parser.c datasource.c ds/ping.c $(PING_SRC) ds/cpu.c ds/memory.c ds/snmp.c ds/if_thr.c ds/loadavg.c ds/shell.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = plottool

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) ds/sunos-ping.o ds/unix-ping.o ds/sryze-ping.o

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# macOS app bundle variables
APP_NAME = PlotTool
BUNDLE_DIR = $(APP_NAME).app
BUNDLE_CONTENTS = $(BUNDLE_DIR)/Contents
BUNDLE_MACOS = $(BUNDLE_CONTENTS)/MacOS
BUNDLE_RESOURCES = $(BUNDLE_CONTENTS)/Resources

# Create macOS .app bundle
app: $(TARGET)
	mkdir -p $(BUNDLE_MACOS)
	mkdir -p $(BUNDLE_RESOURCES)
	cp $(TARGET) $(BUNDLE_MACOS)/$(APP_NAME)
	echo '<?xml version="1.0" encoding="UTF-8"?>' > $(BUNDLE_CONTENTS)/Info.plist
	echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '<plist version="1.0">' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '<dict>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<key>CFBundleExecutable</key>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<string>$(APP_NAME)</string>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<key>CFBundleIdentifier</key>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<string>com.example.plottool</string>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<key>CFBundleName</key>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<string>$(APP_NAME)</string>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<key>CFBundlePackageType</key>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<string>APPL</string>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<key>CFBundleVersion</key>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '	<string>1.0</string>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '</dict>' >> $(BUNDLE_CONTENTS)/Info.plist
	echo '</plist>' >> $(BUNDLE_CONTENTS)/Info.plist

# Create .dmg from .app bundle
dmg: app
	rm -f $(APP_NAME).dmg
	hdiutil create -srcfolder $(BUNDLE_DIR) -volname "$(APP_NAME)" -format UDZO $(APP_NAME).dmg

.PHONY: all clean install app dmg
