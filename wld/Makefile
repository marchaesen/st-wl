# wld: Makefile

include config.mk

PREFIX          ?= /usr/local
LIBDIR          ?= $(PREFIX)/lib
INCLUDEDIR      ?= $(PREFIX)/include
PKGCONFIGDIR    ?= $(LIBDIR)/pkgconfig

PKG_CONFIG      ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

VERSION_MAJOR   := 0
VERSION_MINOR   := 0
VERSION         := $(VERSION_MAJOR).$(VERSION_MINOR)

WLD_LIB_LINK    := libwld.so
WLD_LIB_SONAME  := $(WLD_LIB_LINK).$(VERSION_MAJOR)
WLD_LIB         := $(WLD_LIB_LINK).$(VERSION)

CLEAN_FILES     :=

WLD_REQUIRES = fontconfig pixman-1
WLD_REQUIRES_PRIVATE = freetype2
WLD_SOURCES =           \
    buffer.c            \
    buffered_surface.c  \
    color.c             \
    context.c           \
    font.c              \
    renderer.c          \
    surface.c
WLD_HEADERS = wld.h

ifeq ($(ENABLE_PIXMAN),1)
    WLD_SOURCES += pixman.c
    WLD_HEADERS += pixman.h
endif

ifeq ($(ENABLE_WAYLAND),1)
    WLD_REQUIRES_PRIVATE += wayland-client
    WLD_SOURCES += wayland.c
    WLD_HEADERS += wayland.h

    WLD_SOURCES += wayland-shm.c
endif

ifeq ($(if $(V),$(V),0), 0)
    define quiet
        @echo "  $1	$@"
        @$(if $2,$2,$($1))
    endef
else
    quiet = $(if $2,$2,$($1))
endif

WLD_STATIC_OBJECTS  = $(WLD_SOURCES:%.c=%.o)
WLD_SHARED_OBJECTS  = $(WLD_SOURCES:%.c=%.lo)
WLD_PACKAGES        = $(WLD_REQUIRES) $(WLD_REQUIRES_PRIVATE)
WLD_PACKAGE_CFLAGS ?= $(call pkgconfig,$(WLD_PACKAGES),cflags,CFLAGS)
WLD_PACKAGE_LIBS   ?= $(call pkgconfig,$(WLD_PACKAGES),libs,LIBS)

FINAL_CFLAGS = $(CFLAGS) -fvisibility=hidden -std=c99
FINAL_CPPFLAGS = $(CPPFLAGS) -D_XOPEN_SOURCE=700

# Warning/error flags
FINAL_CFLAGS += -Werror=implicit-function-declaration -Werror=implicit-int \
                -Werror=pointer-sign -Werror=pointer-arith \
                -Wall -Wno-missing-braces

ifeq ($(ENABLE_DEBUG),1)
    FINAL_CPPFLAGS += -DENABLE_DEBUG=1
    FINAL_CFLAGS += -g
else
    FINAL_CPPFLAGS += -DNDEBUG
endif

ifeq ($(ENABLE_STATIC),1)
    TARGETS += libwld.a
    CLEAN_FILES += $(WLD_STATIC_OBJECTS)
endif

CLEAN_FILES += $(TARGETS)

compile     = $(call quiet,CC) $(FINAL_CPPFLAGS) $(FINAL_CFLAGS) -c -o $@ $< \
              -MMD -MP -MF .deps/$(basename $<).d -MT $(basename $@).o -MT $(basename $@).lo
link        = $(call quiet,CCLD,$(CC)) $(LDFLAGS) -o $@ $^
pkgconfig   = $(sort $(foreach pkg,$(1),$(if $($(pkg)_$(3)),$($(pkg)_$(3)), \
                                           $(shell $(PKG_CONFIG) --$(2) $(pkg)))))

.PHONY: all
all: $(TARGETS)

.deps:
	@mkdir "$@"

%.o: %.c | .deps
	$(compile) $(WLD_CPPFLAGS) $(WLD_PACKAGE_CFLAGS)

%.lo: %.c | .deps
	$(compile) $(WLD_CPPFLAGS) $(WLD_PACKAGE_CFLAGS) -fPIC

libwld.a: $(WLD_STATIC_OBJECTS)
	$(call quiet,AR) cr $@ $^

$(WLD_LIB): $(WLD_SHARED_OBJECTS)
	$(link) $(WLD_PACKAGE_LIBS) -shared -Wl,-soname,$(WLD_LIB_SONAME),-no-undefined

$(WLD_LIB_SONAME) $(WLD_LIB_LINK): $(WLD_LIB)
	$(call quiet,SYM,ln -sf) $< $@

$(foreach dir,LIB PKGCONFIG,$(DESTDIR)$($(dir)DIR)) $(DESTDIR)$(INCLUDEDIR)/wld:
	mkdir -p $@

.PHONY: install-libwld.a
install-libwld.a: libwld.a | $(DESTDIR)$(LIBDIR)
	install -m 644 $< $(DESTDIR)$(LIBDIR)

.PHONY: install-$(WLD_LIB)
install-$(WLD_LIB): $(WLD_LIB) | $(DESTDIR)$(LIBDIR)
	install -m 755 $< $(DESTDIR)$(LIBDIR)

.PHONY: install-$(WLD_LIB_LINK) install-$(WLD_LIB_SONAME)
install-$(WLD_LIB_LINK) install-$(WLD_LIB_SONAME): install-$(WLD_LIB) | $(DESTDIR)$(LIBDIR)
	ln -sf $(WLD_LIB) $(DESTDIR)$(LIBDIR)/${@:install-%=%}

.PHONY: install
install: $(TARGETS:%=install-%) | $(foreach dir,LIB PKGCONFIG,$(DESTDIR)$($(dir)DIR)) $(DESTDIR)$(INCLUDEDIR)/wld
	install -m 644 $(WLD_HEADERS) $(DESTDIR)$(INCLUDEDIR)/wld

.PHONY: clean
clean:
	rm -rf $(CLEAN_FILES)

-include .deps/*.d

