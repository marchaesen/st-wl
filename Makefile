# st-wl - simple terminal
# See LICENSE file for copyright and license details.

CC = gcc
CFLAGS = -pipe

ifeq (,$(findstring j,$(MAKEFLAGS)))
CPUS ?= $(shell nproc)
MAKEFLAGS += -j $(CPUS)
endif
include config.mk

THISDIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

ifdef DEBUG
DEBUGFLAGS=-O0 -g -DDEBUG
endif

SRC = st.c wl.c xdg-shell-protocol.c xdg-decoration-protocol.c $(SIXEL_C) $(LIGATURES_C)
OBJ = $(SRC:%.c=.objects/%.o)
DEP = $(SRC:%.c=.deps/%.d)

all: st-wl

options:
	@echo st-wl build options:
	@echo "CFLAGS  = $(STCFLAGS)"
	@echo "LDFLAGS = $(STLDFLAGS)"
	@echo "CC      = $(CC)"
	@touch options

config.h: config.def.h
	cp config.def.h config.h

patches.h: patches.def.h
	cp patches.def.h patches.h

xdg-shell-protocol.c:
	@echo GEN $@
	@wayland-scanner private-code $(XDG_SHELL_PROTO) $@

xdg-shell-client-protocol.h:
	@echo GEN $@
	@wayland-scanner client-header $(XDG_SHELL_PROTO) $@

xdg-decoration-protocol.c:
	@echo GEN $@
	@wayland-scanner private-code $(XDG_DECORATION_PROTO) $@

xdg-decoration-protocol.h:
	@echo GEN $@
	@wayland-scanner client-header $(XDG_DECORATION_PROTO) $@

ifeq ($(if $(V),$(V),0), 0)
    define quiet
			@echo "  $1 $(notdir $(<:%.c=.objects/%.o))"
      @$(if $2,$2,$($1))
    endef
    define quietlink
			@echo "  $1 $@"
      @$(if $2,$2,$($1))
    endef
else
    define quiet
        $(if $2,$2,$($1))
    endef
    define quietlink
        $(if $2,$2,$($1))
    endef
endif

.objects/%.o .deps/%.d: %.c | .deps .objects
	$(call quiet,CC) $(STCFLAGS) -c $< -o .objects/$*.o -MMD -MP -MF .deps/$*.d -MT .objects/$*.o

.objects/wl.o: xdg-shell-client-protocol.h xdg-decoration-protocol.h

$(OBJ): config.h config.mk patches.h

# just to make sure that the options rule is run first
$(THISDIR)wld/libwld.a config.h config.mk patches.h: options

st-wl: $(THISDIR)wld/libwld.a $(OBJ)
	$(call quietlink,CC) $(STCFLAGS) -o $@ $(OBJ) $(STLDFLAGS)

$(THISDIR)wld/.objects/%.o:
	@rm -f $@

$(THISDIR)wld/libwld.a:
	@DEBUGFLAGS="$(DEBUGFLAGS)" $(MAKE) -C wld

.deps .objects:
	@mkdir "$@"

clean:
	rm -rf .objects .deps
	rm -f st-wl st-wl-$(VERSION).tar.gz config.h options patches.h xdg-decoration-* xdg-shell-*

	$(MAKE) -C wld clean

install: st-wl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f st-wl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/st-wl
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < st-wl.1 > $(DESTDIR)$(MANPREFIX)/man1/st-wl.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/st-wl.1
	tic -sx st-wl.info
	mkdir -p $(DESTDIR)$(PREFIX)/share/applications
	cp st-wl.desktop $(DESTDIR)$(PREFIX)/share/applications
	@echo Please see the README file regarding the terminfo entry of st-wl.

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/st-wl
	rm -f $(DESTDIR)$(MANPREFIX)/man1/st-wl.1
	rm -f $(DESTDIR)$(PREFIX)/share/applications/st-wl.desktop

.PHONY: all clean dist install uninstall

-include wld/.deps/*.d
-include $(DEP)

