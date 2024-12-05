# wld: Makefile

CC = gcc
CFLAGS = -pipe

ENABLE_DEBUG = 0

THISDIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

PREFIX          ?= /usr/local
LIBDIR          ?= $(PREFIX)/lib
INCLUDEDIR      ?= $(PREFIX)/include
PKGCONFIGDIR    ?= $(LIBDIR)/pkgconfig

PKG_CONFIG      ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

VERSION_MAJOR   := 0
VERSION_MINOR   := 0
VERSION         := $(VERSION_MAJOR).$(VERSION_MINOR)

CLEAN_FILES     :=

WLD_SOURCES =           \
    $(THISDIR)buffer.c            \
    $(THISDIR)buffered_surface.c  \
    $(THISDIR)color.c             \
    $(THISDIR)context.c           \
    $(THISDIR)font.c              \
    $(THISDIR)renderer.c          \
    $(THISDIR)surface.c           \
    $(THISDIR)pixman.c            \
    $(THISDIR)wayland.c           \
    $(THISDIR)wayland-shm.c


ifeq ($(if $(V),$(V),0), 0)
    define quiet
        @echo "  $1	$@"
        @$(if $2,$2,$($1))
    endef
else
    quiet = $(if $2,$2,$($1))
endif

WLD_STATIC_OBJECTS  = $(WLD_SOURCES:%.c=%.o)
WLD_PACKAGES        = fontconfig pixman-1 freetype2 wayland-client
WLD_PACKAGE_CFLAGS ?= $(call pkgconfig,$(WLD_PACKAGES),cflags,CFLAGS)
WLD_PACKAGE_LIBS   ?= $(call pkgconfig,$(WLD_PACKAGES),libs,LIBS)

FINAL_CFLAGS = $(DEBUGFLAGS) $(CFLAGS) -fvisibility=hidden -std=c99
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

TARGETS += libwld.a
CLEAN_FILES += $(WLD_STATIC_OBJECTS)

CLEAN_FILES += $(TARGETS)

compile     = $(call quiet,CC) $(FINAL_CPPFLAGS) $(FINAL_CFLAGS) -c -o $@ $< \
              -MMD -MP -MF .deps/$(basename $(notdir $<)).d -MT $(basename $@).o
link        = $(call quiet,CCLD,$(CC)) $(LDFLAGS) -o $@ $^
pkgconfig   = $(sort $(foreach pkg,$(1),$(if $($(pkg)_$(3)),$($(pkg)_$(3)), \
                                           $(shell $(PKG_CONFIG) --$(2) $(pkg)))))

.PHONY: all
all: $(TARGETS)

.deps:
	@mkdir "$@"

$(THISDIR)%.o $(THISDIR)%.d: $(THISDIR)%.c | .deps
	$(compile) $(WLD_CPPFLAGS) $(WLD_PACKAGE_CFLAGS)

libwld.a: $(WLD_STATIC_OBJECTS)
	$(call quiet,AR) cr $@ $^
	@echo $(THISDIR)libwld.a: $(WLD_STATIC_OBJECTS) > .deps/libwld.d

$(foreach dir,LIB PKGCONFIG,$(DESTDIR)$($(dir)DIR)) $(DESTDIR)$(INCLUDEDIR)/wld:
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf $(CLEAN_FILES)

-include .deps/*.d

