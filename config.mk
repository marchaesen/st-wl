# st version
VERSION = 0.9.2

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
ICONPREFIX = $(PREFIX)/share/pixmaps
ICONNAME = st.png

PKG_CONFIG = pkg-config
PKGCFG = fontconfig wayland-client wayland-cursor xkbcommon pixman-1 libdrm_intel libdrm_nouveau
XDG_SHELL_PROTO = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`/stable/xdg-shell/xdg-shell.xml
XDG_DECORATION_PROTO = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

PKG_CONFIG = pkg-config

# Uncomment this for the alpha patch / ALPHA_PATCH
XRENDER = `$(PKG_CONFIG) --libs xrender`

# Uncomment this for the themed cursor patch / THEMED_CURSOR_PATCH
#XCURSOR = `$(PKG_CONFIG) --libs xcursor`

# Uncomment the lines below for the ligatures patch / LIGATURES_PATCH
#LIGATURES_C = hb.c
#LIGATURES_H = hb.h
#LIGATURES_INC = `$(PKG_CONFIG) --cflags harfbuzz`
#LIGATURES_LIBS = `$(PKG_CONFIG) --libs harfbuzz`

# Uncomment this for the SIXEL patch / SIXEL_PATCH
SIXEL_C = sixel.c sixel_hls.c
SIXEL_LIBS = `$(PKG_CONFIG) --libs imlib2`

# Uncomment for the netwmicon patch / NETWMICON_PATCH
#NETWMICON_LIBS = `$(PKG_CONFIG) --libs gdlib`

# includes and libs, uncomment harfbuzz for the ligatures patch
INCS = -I. -I/usr/include `$(PKG_CONFIG) --cflags ${PKGCFG}` \
       `$(PKG_CONFIG) --cflags fontconfig` \
       `$(PKG_CONFIG) --cflags freetype2` \
       $(LIGATURES_INC)
LIBS = -L/usr/lib -lc -lm -lrt -lutil `$(PKG_CONFIG) --libs ${PKGCFG}`\
       `$(PKG_CONFIG) --libs fontconfig` \
       `$(PKG_CONFIG) --libs freetype2` \
       $(LIGATURES_LIBS) \
       $(NETWMICON_LIBS)

# flags
STCPPFLAGS = -DVERSION=\"$(VERSION)\" -DICON=\"$(ICONPREFIX)/$(ICONNAME)\" -D_XOPEN_SOURCE=700 -DWITH_WAYLAND_DRM -DWITH_WAYLAND_SHM
STCFLAGS = -O3 $(INCS) $(STCPPFLAGS) $(CPPFLAGS) $(CFLAGS)
#STCFLAGS = -O0 -g $(INCS) $(STCPPFLAGS) $(CPPFLAGS) $(CFLAGS)
STLDFLAGS = wld/libwld.a $(LIBS) $(LDFLAGS)

# OpenBSD:
#CPPFLAGS = -DVERSION=\"$(VERSION)\" -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
#LIBS = -L$(X11LIB) -lm -lX11 -lutil -lXft \
#       `pkg-config --libs fontconfig` \
#       `pkg-config --libs freetype2`
#MANPREFIX = ${PREFIX}/man

# compiler and linker
# CC = c99
