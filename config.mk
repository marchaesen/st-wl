# st version
VERSION = 0.8.2

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

PKG_CONFIG = pkg-config
PKGCFG = fontconfig wayland-client wayland-cursor xkbcommon pixman-1 libdrm_intel libdrm_nouveau
XDG_SHELL_PROTO = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`/stable/xdg-shell/xdg-shell.xml

PKG_CONFIG = pkg-config

# includes and libs
INCS = -I. -I/usr/include `$(PKG_CONFIG) --cflags ${PKGCFG}`
LIBS = -L/usr/lib -lc -lm -lrt -lutil `$(PKG_CONFIG) --libs ${PKGCFG}`

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
STCPPFLAGS = -DVERSION=\"$(VERSION)\" -DICON=\"$(ICONPREFIX)/$(ICONNAME)\" -D_XOPEN_SOURCE=700 -DWITH_WAYLAND_DRM -DWITH_WAYLAND_SHM -DWITH_INTEL_DRM -DWITH_NOUVEAU_DRM
#STCFLAGS = -O3 $(INCS) $(STCPPFLAGS) $(CPPFLAGS) $(CFLAGS)
STCFLAGS = -O0 -g $(INCS) $(STCPPFLAGS) $(CPPFLAGS) $(CFLAGS)
STLDFLAGS = wld/libwld.a $(LIBS) $(LDFLAGS)

# OpenBSD:
#CPPFLAGS = -DVERSION=\"$(VERSION)\" -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
#LIBS = -L$(X11LIB) -lm -lX11 -lutil -lXft \
#       `pkg-config --libs fontconfig` \
#       `pkg-config --libs freetype2`

# compiler and linker
# CC = c99
