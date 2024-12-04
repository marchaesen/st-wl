# st-wl - simple terminal
# See LICENSE file for copyright and license details.

include config.mk

ifdef DEBUG
DEBUGFLAGS=-O0 -g
endif

SRC = st.c wl.c xdg-shell-protocol.c xdg-decoration-protocol.c $(LIGATURES_C) # $(SIXEL_C)
OBJ = $(SRC:.c=.o)

all: options st-wl

options:
	@echo st-wl build options:
	@echo "CFLAGS  = $(STCFLAGS)"
	@echo "LDFLAGS = $(STLDFLAGS)"
	@echo "CC      = $(CC)"

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


.c.o:
	$(CC) $(STCFLAGS) -c $<

st.o: config.h st.h win.h
wl.o: arg.h config.h st.h win.h xdg-shell-client-protocol.h xdg-decoration-protocol.h $(LIGATURES_H)

$(OBJ): config.h config.mk patches.h

st-wl: wld/libwld.a $(OBJ)
	$(CC) $(STCFLAGS) -o $@ $(OBJ) $(STLDFLAGS)

wlddepends:= wayland-private.h surface.c color.c buffer.c interface/context.h interface/buffer.h\
						 interface/surface.h pixman.h config.mk renderer.c wayland-shm.c wayland.h wld.h context.c\
						 wld-private.h wayland.c Makefile pixman.c font.c buffered_surface.c renderer.h common.mk

wld/libwld.a: $(wlddepends:%=wld/%)
	DEBUGFLAGS="$(DEBUGFLAGS)" $(MAKE) -C wld

clean:
	rm -f st-wl $(OBJ) st-wl-$(VERSION).tar.gz xdg-shell-*

dist: clean
	mkdir -p st-wl-$(VERSION)
	cp -R FAQ LEGACY TODO LICENSE Makefile README config.mk\
		config.def.h st-wl.info st-wl.1 arg.h st-wl.h win.h $(LIGATURES_H) $(SRC)\
		st-wl-$(VERSION)
	tar -cf - st-wl-$(VERSION) | gzip > st-wl-$(VERSION).tar.gz
	rm -rf st-wl-$(VERSION)

install: st-wl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f st-wl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/st-wl
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < st-wl.1 > $(DESTDIR)$(MANPREFIX)/man1/st-wl.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/st-wl.1
	tic -sx st-wl.info
	mkdir -p $(DESTDIR)$(PREFIX)/share/applications # desktop-entry patch
	cp st-wl.desktop $(DESTDIR)$(PREFIX)/share/applications # desktop-entry patch
	@echo Please see the README file regarding the terminfo entry of st-wl.

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/st-wl
	rm -f $(DESTDIR)$(MANPREFIX)/man1/st-wl.1
	rm -f $(DESTDIR)$(PREFIX)/share/applications/st-wl.desktop # desktop-entry patch

.PHONY: all options clean dist install uninstall
