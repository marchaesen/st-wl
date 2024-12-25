/* See LICENSE for license details. */
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>
/* for BTN_* definitions */
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wld/wld.h>
#include <wld/wayland.h>
#include <wchar.h>
#include <stdbool.h>

char *argv0;
#include "arg.h"
#include "st.h"
#include "win.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-protocol.h"

#if SIXEL_PATCH
#include "sixel.h"
#endif // SIXEL_PATCH

#define DRAW_BUF_SIZ  20*1024

typedef struct {
	uint mask;
	int axis;
	int dir;
	void (*func)(const Arg *);
  const Arg arg;
  int screen;
} Axiskey;

#if CSI_22_23_PATCH
/* size of title stack */
#define TITLESTACKSIZE 8
#endif // CSI_22_23_PATCH

/* Key modifiers */
#define MOD_MASK_ANY    UINT_MAX
#define MOD_MASK_NONE   0
#define MOD_MASK_CTRL   (1<<0)
#define MOD_MASK_ALT    (1<<1)
#define MOD_MASK_SHIFT  (1<<2)
#define MOD_MASK_LOGO   (1<<3)

#define AXIS_VERTICAL   WL_POINTER_AXIS_VERTICAL_SCROLL

/* function definitions used in config.h */
static void clipcopy(const Arg *);
static void clippaste(const Arg *);
static void numlock(const Arg *);
static void selpaste(const Arg *);
static void ttysend(const Arg *);
static void zoom(const Arg *);
static void zoomabs(const Arg *);
static void zoomreset(const Arg *);

#include "patch/st_include.h"
#include "patch/x_include.h"

/* config.h for applying patches and the configuration. */
#include "config.h"

/* Macros */
#define IS_SET(flag)		((win.mode & (flag)) != 0)

typedef struct {
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	struct xkb_compose_table *compose_table;
	struct xkb_compose_state *compose_state;
	xkb_mod_index_t ctrl, alt, shift, logo;
	unsigned int mods;
} XKB;

typedef struct {
	struct wl_display *dpy;
	struct wl_compositor *cmp;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct wl_data_device_manager *datadevmanager;
	struct wl_data_device *datadev;
	struct wl_data_offer *seloffer;
	struct wl_surface *surface;
	struct wl_buffer *buffer;
	struct xdg_wm_base *wm;
	struct xdg_surface *xdgsurface;
	struct xdg_toplevel *xdgtoplevel;
  struct zxdg_decoration_manager_v1 *decoration_manager;
	XKB xkb;
	bool configured;
	int px, py; /* pointer x and y */
	struct wl_callback * framecb;
	uint32_t globalserial; /* global event serial */
	bool needdraw;
  bool resized;
} Wayland;

typedef struct {
	struct wl_data_source *source;
	char *primary;
	uint32_t tclick1;
	uint32_t tclick2;
} WlSelection;

typedef struct {
	struct wl_cursor_theme *theme;
	struct wl_cursor *cursor;
	struct wl_surface *surface;
} Cursor;

typedef struct {
	struct wld_context *ctx;
	struct wld_font_context *fontctx;
	struct wld_renderer *renderer;
	struct wld_buffer *buffer, *oldbuffer;
} WLD;

typedef struct {
	char str[32];
	uint32_t key;
	int len;
	bool started;
	struct timespec last;
} Repeat;

static int evcol(int);
static int evrow(int);

static void wlmousereport(int, bool, int, int);
static void wlmousereportbutton(uint32_t, uint32_t);
static void wlmousereportmotion(wl_fixed_t, wl_fixed_t);
static void wlmousereportaxis(uint32_t, wl_fixed_t);
static void ptrenter(void *, struct wl_pointer *, uint32_t,
		struct wl_surface *, wl_fixed_t, wl_fixed_t);
static void ptrleave(void *, struct wl_pointer *, uint32_t,
		struct wl_surface *);
static void ptrmotion(void *, struct wl_pointer *, uint32_t,
		wl_fixed_t, wl_fixed_t);
static void ptrbutton(void *, struct wl_pointer *, uint32_t, uint32_t,
		uint32_t, uint32_t);
static void ptraxis(void *, struct wl_pointer *, uint32_t, uint32_t,
		wl_fixed_t);
static void mousesel(int);
static void wlselpaste(void);
static void setsel(char*, uint32_t);
static inline void selwritebuf(char *, int);

static int match(uint, uint);
static const char *kmap(xkb_keysym_t, uint);
static void kbdkeymap(void *, struct wl_keyboard *, uint32_t, int32_t,
		uint32_t);
static void kbdenter(void *, struct wl_keyboard *, uint32_t,
		struct wl_surface *, struct wl_array *);
static void kbdleave(void *, struct wl_keyboard *, uint32_t,
		struct wl_surface *);
static void kbdkey(void *, struct wl_keyboard *, uint32_t, uint32_t, uint32_t,
		uint32_t);
static void kbdmodifiers(void *, struct wl_keyboard *, uint32_t, uint32_t,
		uint32_t, uint32_t, uint32_t);
static void kbdrepeatinfo(void *, struct wl_keyboard *, int32_t, int32_t);

static void wlresize(int, int);
static void cresize(int, int);
static void surfenter(void *, struct wl_surface *, struct wl_output *);
static void surfleave(void *, struct wl_surface *, struct wl_output *);
static void framedone(void *, struct wl_callback *, uint32_t);
static void xdgsurfconfigure(void *, struct xdg_surface *, uint32_t);
static void xdgtoplevelconfigure(void *, struct xdg_toplevel *,
		int32_t, int32_t, struct wl_array *);
static void xdgtoplevelclose(void *, struct xdg_toplevel *);
static void wmping(void *, struct xdg_wm_base *, uint32_t);

static inline uchar sixd_to_8bit(int);
static int wlloadfont(Font *, FcPattern *);
static void wlunloadfont(Font *f);
static void wlloadfonts(char *, double);
static void wlunloadfonts(void);

static void wlclear(int, int, int, int);
static void wldraws(char *, Glyph, int, int, int, int);
static void wldrawglyph(Glyph, int, int);
static void wlloadcursor(void);

static void regglobal(void *, struct wl_registry *, uint32_t, const char *,
		uint32_t);
static void regglobalremove(void *, struct wl_registry *, uint32_t);
static void datadevoffer(void *, struct wl_data_device *,
		struct wl_data_offer *);
static void datadeventer(void *, struct wl_data_device *, uint32_t,
		struct wl_surface *, wl_fixed_t, wl_fixed_t, struct wl_data_offer *);
static void datadevleave(void *, struct wl_data_device *);
static void datadevmotion(void *, struct wl_data_device *, uint32_t,
		wl_fixed_t x, wl_fixed_t y);
static void datadevdrop(void *, struct wl_data_device *);
static void datadevselection(void *, struct wl_data_device *,
		struct wl_data_offer *);
static void dataofferoffer(void *, struct wl_data_offer *, const char *);
static void datasrctarget(void *, struct wl_data_source *, const char *);
static void datasrcsend(void *, struct wl_data_source *, const char *,
		int32_t);
static void datasrccancelled(void *, struct wl_data_source *);

static void wlinit(int, int);
static void run(void);
static void usage(void);

/* Globals */
DC dc;
static Wayland wl;
static WlSelection wlsel;
TermWindow win;
static WLD wld;
static Cursor cursor;
static Repeat repeat;
static int oldx, oldy;

#if CSI_22_23_PATCH
static int tstki; /* title stack index */
static char *titlestack[TITLESTACKSIZE]; /* title stack */
#endif // CSI_22_23_PATCH

static struct wl_callback_listener framelistener = { framedone };
static struct wl_registry_listener reglistener = { regglobal,
		regglobalremove };
static struct wl_surface_listener surflistener = { surfenter, surfleave };
static struct wl_keyboard_listener kbdlistener = { kbdkeymap, kbdenter,
		kbdleave, kbdkey, kbdmodifiers, kbdrepeatinfo };
static struct wl_pointer_listener ptrlistener = { ptrenter, ptrleave,
		ptrmotion, ptrbutton, ptraxis};
static struct xdg_wm_base_listener wmlistener = { wmping };
static struct xdg_surface_listener xdgsurflistener = { xdgsurfconfigure };
static struct xdg_toplevel_listener xdgtoplevellistener = {
		xdgtoplevelconfigure, xdgtoplevelclose };
static struct wl_data_device_listener datadevlistener = { datadevoffer,
		datadeventer, datadevleave, datadevmotion, datadevdrop, datadevselection };
static struct wl_data_source_listener datasrclistener = { datasrctarget,
		datasrcsend, datasrccancelled };
static struct wl_data_offer_listener dataofferlistener = { dataofferoffer };

/* Font Ring Cache */
enum {
	FRC_NORMAL,
	FRC_ITALIC,
	FRC_BOLD,
	FRC_ITALICBOLD
};

typedef struct {
	struct wld_font *font;
	int flags;
	Rune unicodep;
} Fontcache;

/* Fontcache is an array now. A new font will be appended to the array. */
static Fontcache *frc = NULL;
static int frclen = 0;
static int frccap = 0;
static char *usedfont = NULL;
static double usedfontsize = 0;
static double defaultfontsize = 0;

#if ALPHA_PATCH
static char *opt_alpha = NULL;
#endif // ALPHA_PATCH
static char *opt_class = NULL;
static char **opt_cmd  = NULL;
static char *opt_embed = NULL;
static char *opt_font  = NULL;
static char *opt_io    = NULL;
static char *opt_line  = NULL;
static char *opt_name  = NULL;
static char *opt_title = NULL;
#if WORKINGDIR_PATCH
static char *opt_dir   = NULL;
#endif // WORKINGDIR_PATCH

#if ALPHA_PATCH && ALPHA_FOCUS_HIGHLIGHT_PATCH
static int focused = 0;
#endif // ALPHA_FOCUS_HIGHLIGHT_PATCH

#if BLINKING_CURSOR_PATCH
static int cursorblinks = 0;
#endif // BLINKING_CURSOR_PATCH

static int oldbutton = 3; /* button event on startup: 3 = release */

void
xbell(void)
{
	/* Do nothing, no bell in wayland. */
}

void
xsetpointermotion(int dummy)
{
	/* Do nothing, not required under wayland. */
}

void
xsetmode(int set, unsigned int flags)
{
	int mode = win.mode;
	MODBIT(win.mode, set, flags);
	if ((win.mode & MODE_REVERSE) != (mode & MODE_REVERSE))
		redraw();
}

void
numlock(const Arg *dummy)
{
	win.mode ^= MODE_NUMLOCK;
}

int
xsetcursor(int cursor)
{
	#if BLINKING_CURSOR_PATCH
	if (!BETWEEN(cursor, 0, 8)) /* 7-8: st extensions */
	#else
	if (!BETWEEN(cursor, 0, 7)) /* 7: st extension */
	#endif // BLINKING_CURSOR_PATCH
		return 1;
	#if DEFAULT_CURSOR_PATCH
	#if BLINKING_CURSOR_PATCH
	win.cursor = (cursor ? cursor : cursorstyle);
	#else
	win.cursor = (cursor ? cursor : cursorshape);
	#endif // BLINKING_CURSOR_PATCH
	#else
	win.cursor = cursor;
	#endif // DEFAULT_CURSOR_PATCH
	#if BLINKING_CURSOR_PATCH
	cursorblinks = win.cursor == 0 || win.cursor == 1 ||
	               win.cursor == 3 || win.cursor == 5 ||
	               win.cursor == 7;
	#endif // BLINKING_CURSOR_PATCH
	return 0;
}

int
evcol(int x)
{
	#if ANYSIZE_PATCH
	x -= win.hborderpx;
	#else
	x -= borderpx;
	#endif // ANYSIZE_PATCH
	LIMIT(x, 0, win.tw - 1);
	return x / win.cw;
}

int
evrow(int y)
{
	#if ANYSIZE_PATCH
	y -= win.vborderpx;
	#else
	y -= borderpx;
	#endif // ANYSIZE_PATCH
	LIMIT(y, 0, win.th - 1);
	return y / win.ch;
}

void
wlmousereport(int button, bool release, int x, int y)
{
	int len;
	char buf[40];

	if (!IS_SET(MODE_MOUSEX10)) {
		button += ((wl.xkb.mods & MOD_MASK_SHIFT) ? 4  : 0)
			+ ((wl.xkb.mods & MOD_MASK_LOGO ) ? 8  : 0)
			+ ((wl.xkb.mods & MOD_MASK_CTRL ) ? 16 : 0);
	}

	if (IS_SET(MODE_MOUSESGR)) {
		len = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
				button, x+1, y+1, release ? 'm' : 'M');
	} else if (x < 223 && y < 223) {
		len = snprintf(buf, sizeof(buf), "\033[M%c%c%c",
				32+button, 32+x+1, 32+y+1);
	} else {
		return;
	}

	ttywrite(buf, len, 0);
}

void
wlmousereportbutton(uint32_t button, uint32_t state)
{
	bool release = state == WL_POINTER_BUTTON_STATE_RELEASED;

	if (!IS_SET(MODE_MOUSESGR) && release) {
		button = 3;
	} else {
		switch (button) {
			case BTN_LEFT:
				button = 0;
				break;
			case BTN_MIDDLE:
				button = 1;
				break;
			case BTN_RIGHT:
				button = 2;
				break;
		}
	}

	oldbutton = release ? 3 : button;

	/* don't report release events when in X10 mode */
	if (IS_SET(MODE_MOUSEX10) && release) {
		return;
	}

	wlmousereport(button, release, oldx, oldy);
}

void
wlmousereportmotion(wl_fixed_t fx, wl_fixed_t fy)
{
	int x = evcol(wl_fixed_to_int(fx)), y = evrow(wl_fixed_to_int(fy));

	if (x == oldx && y == oldy)
		return;

  oldx = x;
	oldy = y;

	if (!IS_SET(MODE_MOUSEMOTION) && !IS_SET(MODE_MOUSEMANY))
		return;
	/* MOUSE_MOTION: no reporting if no button is pressed */
	if (IS_SET(MODE_MOUSEMOTION) && oldbutton == 3)
		return;

	wlmousereport(oldbutton + 32, false, x, y);
}

void
wlmousereportaxis(uint32_t axis, wl_fixed_t amount)
{
	wlmousereport(64 + (axis == AXIS_VERTICAL ? 4 : 6)
			+ (amount > 0 ? 1 : 0) - 4, false, oldx, oldy);
}

void
ptrenter(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
	struct wl_cursor_image *img = cursor.cursor->images[0];
	struct wl_buffer *buffer;

	wl_pointer_set_cursor(pointer, serial, cursor.surface,
			img->hotspot_x, img->hotspot_y);
	buffer = wl_cursor_image_get_buffer(img);
	wl_surface_attach(cursor.surface, buffer, 0, 0);
	wl_surface_damage(cursor.surface, 0, 0, img->width, img->height);
	wl_surface_commit(cursor.surface);
}

void
ptrleave(void *data, struct wl_pointer *pointer, uint32_t serial,
		struct wl_surface *surface)
{
}

void
ptrmotion(void *data, struct wl_pointer * pointer, uint32_t serial,
		wl_fixed_t x, wl_fixed_t y)
{
	if (IS_SET(MODE_MOUSE) && !(wl.xkb.mods & forceselmod)) {
		wlmousereportmotion(x, y);
		return;
	}

	wl.px = wl_fixed_to_int(x);
	wl.py = wl_fixed_to_int(y);

	mousesel(0);
}

void
ptrbutton(void * data, struct wl_pointer * pointer, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state)
{
	int snap;

	if (IS_SET(MODE_MOUSE) && !(wl.xkb.mods & forceselmod)) {
		wlmousereportbutton(button, state);
		return;
	}

	switch (state) {
		case WL_POINTER_BUTTON_STATE_RELEASED:
			if (button == BTN_MIDDLE) {
	#if CLIPBOARD_PATCH
	      clippaste(NULL);
	#else
				wlselpaste();
	{ BTN_MIDDLE, MOD_MASK_ANY, "", selpaste},
	#endif // CLIPBOARD_PATCH
			} else if (button == BTN_LEFT) {
				wl.globalserial = serial;
				mousesel(1);
			}
			break;

		case WL_POINTER_BUTTON_STATE_PRESSED:
			if (button == BTN_LEFT) {
				/*
				 * If the user clicks below predefined timeouts
				 * specific snapping behaviour is exposed.
				 */
				if (time - wlsel.tclick2 <= tripleclicktimeout) {
					snap = SNAP_LINE;
				} else if (time - wlsel.tclick1 <= doubleclicktimeout) {
					snap = SNAP_WORD;
				} else {
					snap = 0;
				}
				wlsel.tclick2 = wlsel.tclick1;
				wlsel.tclick1 = time;

				selstart(evcol(wl.px), evrow(wl.py), snap);
			}
			break;
	}
}

void
ptraxis(void * data, struct wl_pointer * pointer, uint32_t time, uint32_t axis,
		wl_fixed_t value)
{
	int dir = value > 0 ? +1 : -1;
	int screen = tisaltscr() ? S_ALT : S_PRI;

	if (IS_SET(MODE_MOUSE) && !(wl.xkb.mods & forceselmod)) {
		wlmousereportaxis(axis, value);
		return;
	}

	for (Axiskey *ak = ashortcuts; ak < ashortcuts + LEN(ashortcuts); ak++) {
		if (axis == ak->axis && dir == ak->dir
        && (!ak->screen || (ak->screen == screen))
				&& match(ak->mask, wl.xkb.mods)) {
			ak->func(&(ak->arg));
			return;
		}
	}
}

void
mousesel(int done)
{
	int type, seltype = SEL_REGULAR;
	uint state = wl.xkb.mods & ~forceselmod;

	for (type = 1; type < LEN(selmasks); ++type) {
		if (match(selmasks[type], state)) {
			seltype = type;
			break;
		}
	}

	selextend(evcol(wl.px), evrow(wl.py), seltype, done);
	if (done)
		setsel(getsel(), wl.globalserial);
}


void
setsel(char *str, uint32_t serial)
{
  if (!str)
    return;

  free(wlsel.primary);
  wlsel.primary = str;

  if (str) {
    wlsel.source = wl_data_device_manager_create_data_source(wl.datadevmanager);
    wl_data_source_add_listener(wlsel.source, &datasrclistener, NULL);
    wl_data_source_offer(wlsel.source, "text/plain");
    wl_data_source_offer(wlsel.source, "text/plain;charset=utf-8");
    wl_data_source_offer(wlsel.source, "TEXT");
    wl_data_source_offer(wlsel.source, "STRING");
    wl_data_source_offer(wlsel.source, "UTF8_STRING");
  } else {
    wlsel.source = NULL;
  }
  wl_data_device_set_selection(wl.datadev, wlsel.source, serial);
  wl_display_roundtrip(wl.dpy);
}

void
selwritebuf(char *buf, int len)
{
	char *repl = buf;

	/*
	 * As seen in getsel:
	 * Line endings are inconsistent in the terminal and GUI world
	 * copy and pasting. When receiving some selection data,
	 * replace all '\n' with '\r'.
	 * FIXME: Fix the computer world.
	 */
	while ((repl = memchr(repl, '\n', len))) {
		*repl++ = '\r';
	}

	ttywrite(buf, len, 1);
}

void
wlselpaste(void)
{
	int fds[2], len, left;
	char buf[BUFSIZ], *str;

	if (wl.seloffer) {
		if (IS_SET(MODE_BRCKTPASTE))
			ttywrite("\033[200~", 6, 0);
		/* check if we are pasting from ourselves */
		if (wlsel.source) {
			str = wlsel.primary;
			left = strlen(wlsel.primary);
			while (left > 0) {
				len = MIN(sizeof buf, left);
				memcpy(buf, str, len);
				selwritebuf(buf, len);
				left -= len;
				str += len;
			}
		} else {
			if (-1==pipe(fds))
        fprintf(stderr, "Error creating pipe");
			wl_data_offer_receive(wl.seloffer, "text/plain", fds[1]);
			wl_display_flush(wl.dpy);
			close(fds[1]);
			while ((len = read(fds[0], buf, sizeof buf)) > 0) {
				selwritebuf(buf, len);
			}
			close(fds[0]);
		}
		if (IS_SET(MODE_BRCKTPASTE))
			ttywrite("\033[201~", 6, 0);
	}
}

#include "patch/x_include.c"

void
xclipcopy(void)
{
	clipcopy(NULL);
}

void
clipcopy(const Arg * dummy)
{
	setsel(getsel(), wl.globalserial);
}

void
clippaste(const Arg * dummy)
{
	wlselpaste();
}

void
selpaste(const Arg * dummy)
{
	wlselpaste();
}

void
xsetsel(char * buf)
{
	setsel(buf, wl.globalserial);
}

int
match(uint mask, uint state)
{
	return mask == MOD_MASK_ANY || mask == (state & ~ignoremod);
}

const char*
kmap(xkb_keysym_t k, uint state)
{
	Key *kp;
	int i;

	/* Check for mapped keys out of X11 function keys. */
	for (i = 0; i < LEN(mappedkeys); i++) {
		if (mappedkeys[i] == k)
			break;
	}
	if (i == LEN(mappedkeys)) {
		if ((k & 0xFFFF) < 0xFD00)
			return NULL;
	}

	for (kp = key; kp < key + LEN(key); kp++) {
		if (kp->k != k)
			continue;

		if (!match(kp->mask, state))
			continue;

		if (IS_SET(MODE_APPKEYPAD) ? kp->appkey < 0 : kp->appkey > 0)
			continue;
		if (IS_SET(MODE_NUMLOCK) && kp->appkey == 2)
			continue;

		if (IS_SET(MODE_APPCURSOR) ? kp->appcursor < 0 : kp->appcursor > 0)
			continue;

		return kp->s;
	}

	return NULL;
}


void
kbdkeymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
		int32_t fd, uint32_t size)
{
	char *string;
	struct xkb_compose_table *compose_table;
	struct xkb_compose_state *compose_state;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	string = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

	if (string == MAP_FAILED) {
		close(fd);
		return;
	}

	wl.xkb.keymap = xkb_keymap_new_from_string(wl.xkb.ctx, string,
			XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(string, size);
	close(fd);
	wl.xkb.state = xkb_state_new(wl.xkb.keymap);

	/* Set up XKB compose table */
	compose_table =
		xkb_compose_table_new_from_locale(wl.xkb.ctx,
				getenv("LANG"),
				XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (compose_table) {
		/* Set up XKB compose state */
		compose_state = xkb_compose_state_new(compose_table,
				XKB_COMPOSE_STATE_NO_FLAGS);
		if (compose_state) {
			xkb_compose_state_unref(wl.xkb.compose_state);
			xkb_compose_table_unref(wl.xkb.compose_table);
			wl.xkb.compose_state = compose_state;
			wl.xkb.compose_table = compose_table;
		} else {
			fprintf(stderr, "could not create XKB compose state.  "
					"Disabiling compose.\n");
			xkb_compose_table_unref(compose_table);
			compose_table = NULL;
		}
	} else {
		fprintf(stderr, "could not create XKB compose table for locale '%s'.  "
				"Disabiling compose\n", getenv("LANG"));
	}

	wl.xkb.ctrl = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_CTRL);
	wl.xkb.alt = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_ALT);
	wl.xkb.shift = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_SHIFT);
	wl.xkb.logo = xkb_keymap_mod_get_index(wl.xkb.keymap, XKB_MOD_NAME_LOGO);

	wl.xkb.mods = 0;
}

static xkb_keysym_t
processKey(Wayland wl, xkb_keysym_t sym)
{
	if (!wl.xkb.compose_state)
		return sym;
	if (sym == XKB_KEY_NoSymbol)
		return sym;
	if (xkb_compose_state_feed(wl.xkb.compose_state,
				sym) != XKB_COMPOSE_FEED_ACCEPTED)
		return sym;

	switch (xkb_compose_state_get_status(wl.xkb.compose_state)) {
		case XKB_COMPOSE_COMPOSING:
			return XKB_KEY_NoSymbol;
		case XKB_COMPOSE_COMPOSED:
			return xkb_compose_state_get_one_sym(wl.xkb.compose_state);
		case XKB_COMPOSE_CANCELLED:
			return XKB_KEY_NoSymbol;
		case XKB_COMPOSE_NOTHING:
			return sym;
		default:
			return sym;
	}
}

void
kbdenter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys)
{
	win.mode |= MODE_FOCUSED;
	if (IS_SET(MODE_FOCUS))
		ttywrite("\033[I", 3, 0);
	/* need to redraw the cursor */
	wlneeddraw();
}

void
kbdleave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface)
{
	/* selection offers are invalidated when we lose keyboard focus */
	wl.seloffer = NULL;
	win.mode &= ~MODE_FOCUSED;
	if (IS_SET(MODE_FOCUS))
		ttywrite("\033[O", 3, 0);
	/* need to redraw the cursor */
	wlneeddraw();
	/* disable key repeat */
	repeat.len = 0;
}

void
kbdkey(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state)
{
	xkb_keysym_t ksym;
	const xkb_keysym_t *ksyms;
	uint32_t num_ksyms;
	char buf[32];
  const char *str;
	int len;
	Rune c;
	Shortcut *bp;

	#if HIDECURSOR_PATCH
	if (xw.pointerisvisible && hidecursor) {
		#if OPENURLONCLICK_PATCH
		#if ANYSIZE_PATCH
		int x = e->x - win.hborderpx;
		int y = e->y - win.vborderpx;
		#else
		int x = e->x - borderpx;
		int y = e->y - borderpx;
		#endif // ANYSIZE_PATCH
		LIMIT(x, 0, win.tw - 1);
		LIMIT(y, 0, win.th - 1);
		if (!detecturl(x / win.cw, y / win.ch, 0)) {
			XDefineCursor(xw.dpy, xw.win, xw.bpointer);
			xsetpointermotion(1);
			xw.pointerisvisible = 0;
		}
		#else
		XDefineCursor(xw.dpy, xw.win, xw.bpointer);
		xsetpointermotion(1);
		xw.pointerisvisible = 0;
		#endif // OPENURLONCLICK_PATCH
	}
	#endif // HIDECURSOR_PATCH

	if (IS_SET(MODE_KBDLOCK))
		return;

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		if (repeat.key == key)
			repeat.len = 0;
		return;
	}

	wl.globalserial = serial;
	num_ksyms = xkb_state_key_get_syms(wl.xkb.state, key + 8, &ksyms); 

	ksym = XKB_KEY_NoSymbol;
	if (num_ksyms == 1)
		ksym = ksyms[0];

	ksym = processKey(wl, ksym);

	len = xkb_keysym_to_utf8(ksym, buf, sizeof buf);
	if (len > 0)
		--len;

	/* 1. shortcuts */
	for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
		if (ksym == bp->keysym && match(bp->mod, wl.xkb.mods)) {
			bp->func(&(bp->arg));
			return;
		}
	}

	/* 2. custom keys from config.h */
	if ((str = kmap(ksym, wl.xkb.mods))) {
		len = strlen(str);
		goto send;
	}

	/* 3. composed string from input method */
	if (len == 0)
		return;
	if (len == 1 && wl.xkb.mods & MOD_MASK_ALT) {
		if (IS_SET(MODE_8BIT)) {
			if (*buf < 0177) {
				c = *buf | 0x80;
				len = utf8encode(c, buf);
			}
		} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			len = 2;
		}
	}
	/* convert character to control character */
	else if (len == 1 && wl.xkb.mods & MOD_MASK_CTRL) {
		if ((*buf >= '@' && *buf < '\177') || *buf == ' ')
			*buf &= 0x1F;
		else if (*buf == '2') *buf = '\000';
		else if (*buf >= '3' && *buf <= '7')
			*buf -= ('3' - '\033');
		else if (*buf == '8') *buf = '\177';
		else if (*buf == '/') *buf = '_' & 0x1F;
	}

	str = buf;

send:
	memcpy(repeat.str, str, len);
	repeat.key = key;
	repeat.len = len;
	repeat.started = false;
	clock_gettime(CLOCK_MONOTONIC, &repeat.last);
	ttywrite(str, len, 1);
}

void
kbdmodifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
		uint32_t dep, uint32_t lat, uint32_t lck, uint32_t group)
{
	xkb_mod_mask_t mod_mask;

	xkb_state_update_mask(wl.xkb.state, dep, lat, lck, group, 0, 0);

	mod_mask = xkb_state_serialize_mods(wl.xkb.state, XKB_STATE_MODS_EFFECTIVE);
	wl.xkb.mods = 0;

	if (mod_mask & (1 << wl.xkb.ctrl))
		wl.xkb.mods |= MOD_MASK_CTRL;
	if (mod_mask & (1 << wl.xkb.alt))
		wl.xkb.mods |= MOD_MASK_ALT;
	if (mod_mask & (1 << wl.xkb.shift))
		wl.xkb.mods |= MOD_MASK_SHIFT;
	if (mod_mask & (1 << wl.xkb.logo))
		wl.xkb.mods |= MOD_MASK_LOGO;
}

void
kbdrepeatinfo(void *data, struct wl_keyboard *keyboard, int32_t rate,
		int32_t delay)
{
	keyrepeatdelay = delay;
	keyrepeatinterval = 1000 / rate;
}

void
cresize(int width, int height)
{
	int col, row;

	if (width != 0)
		win.w = width;
	if (height != 0)
		win.h = height;

	col = (win.w - 2 * borderpx) / win.cw;
	row = (win.h - 2 * borderpx) / win.ch;
	col = MAX(1, col);
	row = MAX(1, row);

	#if ANYSIZE_PATCH
	win.hborderpx = (win.w - col * win.cw) / 2;
	win.vborderpx = (win.h - row * win.ch) / 2;
	#endif // ANYSIZE_PATCH

	tresize(col, row);
	wlresize(col, row);
	ttyresize(win.tw, win.th);
}

void
ttysend(const Arg *arg)
{
	ttywrite(arg->s, strlen(arg->s), 1);
}

void
zoom(const Arg *arg)
{
	Arg larg;

	larg.f = usedfontsize + arg->f;
	#if SIXEL_PATCH
	if (larg.f >= 1.0)
		zoomabs(&larg);
	#else
	zoomabs(&larg);
  #endif
}

void
zoomabs(const Arg *arg)
{
	#if SIXEL_PATCH
	int i;
	ImageList *im;
	#endif // SIXEL_PATCH

	wlunloadfonts();
	wlloadfonts(usedfont, arg->f);

  #if SIXEL_PATCH
	/* delete old pixmaps so that xfinishdraw() can create new scaled ones */
	for (im = term.images, i = 0; i < 2; i++, im = term.images_alt) {
		for (; im; im = im->next) {
			if (im->pixmap)
				pixman_image_unref(im->pixmap);
			if (im->clipmask)
				pixman_image_unref(im->clipmask);
			im->pixmap = NULL;
			im->clipmask = NULL;
		}
	}
	#endif // SIXEL_PATCH

	cresize(0, 0);
	redraw();
	/* XXX: Should the window size be updated here because wayland doesn't
	 *   * have a notion of hints?
	 *       * xhints(); */
}

void
zoomreset(const Arg *arg)
{
	Arg larg;
	if (defaultfontsize > 0) {
		larg.f = defaultfontsize;
		zoomabs(&larg);
	}
}

void
wlresize(int col, int row)
{
	union wld_object object;

	win.tw = col * win.cw;
	win.th = row * win.ch;

	wld.oldbuffer = wld.buffer;
	wld.buffer = wld_create_buffer(wld.ctx, win.w, win.h,
			WLD_FORMAT_ARGB8888, 0);
	wld_export(wld.buffer, WLD_WAYLAND_OBJECT_BUFFER, &object);
	wl.buffer = object.ptr;
  wl.resized = true;
}

#if CSI_22_23_PATCH
void
xsettitle(char *p, int pop)
{
	free(titlestack[tstki]);
	if (pop) {
		titlestack[tstki] = NULL;
		tstki = (tstki - 1 + TITLESTACKSIZE) % TITLESTACKSIZE;
		p = titlestack[tstki] ? titlestack[tstki] : opt_title;
	} else if (p && p[0] != '\0') {
		titlestack[tstki] = xstrdup(p);
	} else {
		titlestack[tstki] = NULL;
		p = opt_title;
	}
	xdg_toplevel_set_title(wl.xdgtoplevel, p);
}

void
xpushtitle(void)
{
	int tstkin = (tstki + 1) % TITLESTACKSIZE;

	free(titlestack[tstkin]);
	titlestack[tstkin] = titlestack[tstki] ? xstrdup(titlestack[tstki]) : NULL;
	tstki = tstkin;
}

void
xfreetitlestack(void)
{
	for (int i = 0; i < LEN(titlestack); i++) {
		free(titlestack[i]);
		titlestack[i] = NULL;
	}
}
#else
void
xsettitle(char *title)
{
	DEFAULT(title, opt_title);
	xdg_toplevel_set_title(wl.xdgtoplevel, title);
}
#endif

void
surfenter(void *data, struct wl_surface *surface, struct wl_output *output)
{
	if (!(IS_SET(MODE_VISIBLE)))
		win.mode |= MODE_VISIBLE;
}

void
surfleave(void *data, struct wl_surface *surface, struct wl_output *output)
{
	if(IS_SET(MODE_VISIBLE))
		win.mode &= ~MODE_VISIBLE;
}

void
framedone(void *data, struct wl_callback *callback, uint32_t msecs)
{
	wl_callback_destroy(callback);
	wl.framecb = NULL;
	if (wl.needdraw && IS_SET(MODE_VISIBLE)) {
		draw();
	}
}

void
xdgsurfconfigure(void *data, struct xdg_surface *surf, uint32_t serial)
{
	xdg_surface_ack_configure(surf, serial);
}

extern bool g_fullscreen_state;
void
xdgtoplevelconfigure(void *data, struct xdg_toplevel *toplevel,
		int32_t w, int32_t h, struct wl_array *states)
{
  g_fullscreen_state = false;
  if (states)
  {
    uint32_t *state = states->data;
    for (int i = 0; i < states->size/4; i++)
    {
      if (state[i] == XDG_TOPLEVEL_STATE_FULLSCREEN)
      {
        g_fullscreen_state = true;
        break;
      }
    }
  }

	if (w == win.w && h == win.h)
		return;

	cresize(w, h);
	if (!wl.configured)
		wl.configured = true;
}

void
xdgtoplevelclose(void *data, struct xdg_toplevel *toplevel)
{
	/* Send SIGHUP to shell */
	pid_t thispid = getpid();
	kill(thispid, SIGHUP);
	exit(0);
}

void
wmping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
	xdg_wm_base_pong(wm, serial);
}

uchar
sixd_to_8bit(int x)
{
	return x == 0 ? 0 : 0x37 + 0x28 * x;
}

int
wlloadcolor(int i, const char *name, uint32_t *color)
{
	if (!name) {
		if (BETWEEN(i, 16, 255)) { /* 256 color */
			if (i < 6*6*6+16) { /* same colors as xterm */
				*color = 0xff << 24 | sixd_to_8bit(((i-16)/36)%6) << 16
					| sixd_to_8bit(((i-16)/6)%6) << 8
					| sixd_to_8bit(((i-16)/1)%6);
			} else { /* greyscale */
				*color = 0xff << 24 | (0x8 + 0xa * (i-(6*6*6+16))) * 0x10101;
			}
			return true;
		} else
			name = colorname[i];
	}

	return wld_lookup_named_color(name, color);
}

void
xloadcols(void)
{
	int i;
	static int loaded;

	if(!loaded) {
		dc.collen = MAX(LEN(colorname), 256);
		dc.col = xmalloc(dc.collen * sizeof(uint32_t));
	}

	for (i = 0; i < dc.collen; i++)
		if (!wlloadcolor(i, NULL, &dc.col[i])) {
			if (colorname[i])
				die("Could not allocate color '%s'\n", colorname[i]);
			else
				die("Could not allocate color %d\n", i);
		}

	#if ALPHA_PATCH
	/* set alpha value of bg color */
	if (opt_alpha)
  {
		alpha = strtof(opt_alpha, NULL);
  }
  term_alpha = (uint8_t)(alpha*255.0);
	#endif // ALPHA_PATCH
	loaded = 1;
}

int
xgetcolor(int x, unsigned char *r, unsigned char *g, unsigned char *b)
{
	if (!BETWEEN(x, 0, dc.collen))
		return 1;

	*r = (dc.col[x]>>16)&0xff;
	*g = (dc.col[x]>>8)&0xff;
	*b = dc.col[x]&0xff;

	return 0;
}

int
xsetcolorname(int x, const char *name)
{
	uint32_t color;

	if (!BETWEEN(x, 0, dc.collen))
		return 1;


	if (!wlloadcolor(x, name, &color))
		return 1;

	dc.col[x] = color;

	return 0;
}

int
wlloadfont(Font *f, FcPattern *pattern)
{
	FcPattern *configured;
	FcPattern *match;
	FcResult result;
	struct wld_extents extents;
	int wantattr, haveattr;

	/*
	 * Manually configure instead of calling XftMatchFont
	 * so that we can use the configured pattern for
	 * "missing glyph" lookups.
	 */
	configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;

	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	FcDefaultSubstitute(configured);

	match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return 1;
	}

	if (!(f->match = wld_font_open_pattern(wld.fontctx, match))) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return 1;
	}

	if ((FcPatternGetInteger(pattern, "slant", 0, &wantattr) ==
				FcResultMatch)) {
		/*
		 * Check if xft was unable to find a font with the appropriate
		 * slant but gave us one anyway. Try to mitigate.
		 */
		if ((FcPatternGetInteger(match, "slant", 0,
						&haveattr) != FcResultMatch) || haveattr < wantattr) {
			f->badslant = 1;
			fputs("font slant does not match\n", stderr);
		}
	}

	if ((FcPatternGetInteger(pattern, "weight", 0, &wantattr) ==
				FcResultMatch)) {
		if ((FcPatternGetInteger(match, "weight", 0,
						&haveattr) != FcResultMatch) || haveattr != wantattr) {
			f->badweight = 1;
			fputs("font weight does not match\n", stderr);
		}
	}


	wld_font_text_extents(f->match, ascii_printable, &extents);

	f->set = NULL;
	f->pattern = configured;

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance;

	f->height = f->ascent + f->descent;
	f->width = DIVCEIL(extents.advance, strlen(ascii_printable));

	return 0;
}

void
wlloadfonts(char *fontstr, double fontsize)
{
	FcPattern *pattern;
	double fontval;

	if (fontstr[0] == '-') {
		/* XXX: need XftXlfdParse equivalent */
		pattern = NULL;
	} else {
		pattern = FcNameParse((FcChar8 *)fontstr);
	}

	if (!pattern)
		die("can't open font %s\n", fontstr);

	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)fontsize);
		usedfontsize = fontsize;
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = fontval;
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) ==
				FcResultMatch) {
			usedfontsize = -1;
		} else {
			/*
			 * Default font size is 12, if none given. This is to
			 * have a known usedfontsize value.
			 */
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			usedfontsize = 12;
		}
		defaultfontsize = usedfontsize;
	}

	FcConfigSubstitute(0, pattern, FcMatchPattern);
	FcDefaultSubstitute(pattern);

	if (wlloadfont(&dc.font, pattern))
		die("can't open font %s\n", fontstr);

	if (usedfontsize < 0) {
		FcPatternGetDouble(dc.font.pattern,
				FC_PIXEL_SIZE, 0, &fontval);
		usedfontsize = fontval;
		if (fontsize == 0)
			defaultfontsize = fontval;
	}

	/* Setting character width and height. */
	win.cw = ceilf(dc.font.width * cwscale);
	win.ch = ceilf(dc.font.height * chscale);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (wlloadfont(&dc.ifont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (wlloadfont(&dc.ibfont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (wlloadfont(&dc.bfont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDestroy(pattern);
}

void
wlunloadfont(Font *f)
{
	wld_font_close(f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
}

void
wlunloadfonts(void)
{
	/* Free the loaded fonts in the font cache.  */
	while (frclen > 0)
		wld_font_close(frc[--frclen].font);

	wlunloadfont(&dc.font);
	wlunloadfont(&dc.bfont);
	wlunloadfont(&dc.ifont);
	wlunloadfont(&dc.ibfont);
}

void
wlneeddraw(void)
{
	wl.needdraw = true;
}

int
xstartdraw(void)
{
	if(IS_SET(MODE_VISIBLE))
  {
		wld_set_target_buffer(wld.renderer, wld.buffer);
    if (wl.resized)
    {
      wl.resized = false;
      wlclear(0, 0, win.w, win.h);
    }
  }
	return IS_SET(MODE_VISIBLE);
}

void
xdrawline(Line line, int x1, int y, int x2)
{
	int ic, ib, x, ox;
	Glyph base, new;
	char buf[DRAW_BUF_SIZ];

	base = line[0];
	ic = ib = ox = 0;
	for (x = x1; x < x2; x++) {
		new = line[x];
		if (new.mode == ATTR_WDUMMY)
			continue;
		if (selected(x, y))
			#if SELECTION_COLORS_PATCH
			new.mode |= ATTR_SELECTED;
      #else
			new.mode ^= ATTR_REVERSE;
      #endif // SELECTION_COLORS_PATCH
		if (ib > 0 && (ATTRCMP(base, new) || ib >= DRAW_BUF_SIZ-UTF_SIZ)) {
			wldraws(buf, base, ox, y, ic, ib);
			ic = ib = 0;
		}
		if (ib == 0) {
			ox = x;
			base = new;
		}

		ib += utf8encode(new.u, buf+ib);
		ic += (new.mode & ATTR_WIDE)? 2 : 1;
	}
	if (ib > 0)
		wldraws(buf, base, ox, y, ic, ib);

	#if ANYSIZE_PATCH
	wl_surface_damage(wl.surface, 0, win.vborderpx + y * win.ch, win.w, win.ch);
  #else
	wl_surface_damage(wl.surface, 0, borderpx + y * win.ch, win.w, win.ch);
  #endif
}

void
xfinishdraw(void)
{
#if SIXEL_PATCH
  ImageList *im, *next;
  int width, height;
  int del, desty, mode, x1, x2, xend;
#if ANYSIZE_PATCH
  int bw = win.hborderpx, bh = win.vborderpx;
#else
  int bw = borderpx, bh = borderpx;
#endif // ANYSIZE_PATCH
  Line line;
#endif // SIXEL_PATCH

#if SIXEL_PATCH
  for (im = term.images; im; im = next) {
    next = im->next;

    /* do not draw or process the image, if it is not visible */
    if (im->x >= term.col || im->y >= term.row || im->y < 0)
      continue;

#if KEYBOARDSELECT_PATCH && REFLOW_PATCH
    /* do not draw the image on the search bar */
    if (im->y == term.row-1 && IS_SET(MODE_KBDSELECT) && kbds_issearchmode())
      continue;
#endif // KEYBOARDSELECT_PATCH

    /* scale the image */
    width = MAX(im->width * win.cw / im->cw, 1);
    height = MAX(im->height * win.ch / im->ch, 1);
    if (!im->pixmap) {
      im->pixmap = pixman_image_create_bits_no_clear(
          PIXMAN_a8r8g8b8, width, height,
          (uint32_t *)im->pixels, width * sizeof(uint32_t));
    }

    if (!im->pixmap)
      continue;

		/* draw only the parts of the image that are not erased */
		#if SCROLLBACK_PATCH || REFLOW_PATCH
		line = TLINE(im->y) + im->x;
		#else
		line = term.line[im->y] + im->x;
		#endif // SCROLLBACK_PATCH || REFLOW_PATCH
		xend = MIN(im->x + im->cols, term.col);
		for (del = 1, x1 = im->x; x1 < xend; x1 = x2) {
			mode = line->mode & ATTR_SIXEL;
			for (x2 = x1 + 1; x2 < xend; x2++) {
				if (((++line)->mode & ATTR_SIXEL) != mode)
					break;
			}
			if (mode) {
        wld_composite_image(wld.renderer, im->pixmap, NULL,
                           bw + x1 * win.cw, bh + im->y * win.ch,
                           (x1 - im->x) * win.cw, 0,
				                   MIN((x2 - x1) * win.cw, width - (x1 - im->x) * win.cw), height
                           );
				del = 0;
			}
		}
		/* if all the parts are erased, we can delete the entire image */
		if (del && im->x + im->cols <= term.col)
			delete_image(im);
  }
#endif // SIXEL_PATCH

  wl.framecb = wl_surface_frame(wl.surface);
  wl_callback_add_listener(wl.framecb, &framelistener, NULL);
  wld_flush(wld.renderer);
  wl_surface_attach(wl.surface, wl.buffer, 0, 0);
  wl_surface_commit(wl.surface);
  /* need to wait to destroy the old buffer until we commit the new
   * buffer */
  if (wld.oldbuffer) {
    wld_buffer_unreference(wld.oldbuffer);
    wld.oldbuffer = 0;
  }
  wl.needdraw = false;
}

void wltermclear(int col1, int row1, int col2, int row2) {
  uint32_t color = dc.col[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
  color = (color & term_alpha << 24) | (color & 0x00FFFFFF);
	#if ANYSIZE_PATCH
  wld_fill_rectangle(wld.renderer, color, win.hborderpx + col1 * win.cw,
                     win.vborderpx + row1 * win.ch, (col2 - col1 + 1) * win.cw,
                     (row2 - row1 + 1) * win.ch);
  #else
  wld_fill_rectangle(wld.renderer, color, borderpx + col1 * win.cw,
                     borderpx + row1 * win.ch, (col2 - col1 + 1) * win.cw,
                     (row2 - row1 + 1) * win.ch);
  #endif
}

/*
 * Absolute coordinates.
 */
void
wlclear(int x1, int y1, int x2, int y2)
{
	uint32_t color = dc.col[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
  color = (color & term_alpha << 24) | (color & 0x00FFFFFF);
	wld_fill_rectangle(wld.renderer, color, x1, y1, x2 - x1, y2 - y1);
}

/*
 * TODO: Implement something like XftDrawGlyphFontSpec in wld, and then apply a
 * similar patch to ae1923d27533ff46400d93765e971558201ca1ee
 */

void
wldraws(char *s, Glyph base, int x, int y, int charlen, int bytelen)
{
	#if ANYSIZE_PATCH
	int winx = win.hborderpx + x * win.cw, winy = win.vborderpx + y * win.ch,
	    width = charlen * win.cw, xp, i;
	#else
	int winx = borderpx + x * win.cw, winy = borderpx + y * win.ch,
	    width = charlen * win.cw, xp, i;
	#endif // ANYSIZE_PATCH
	int frcflags, charexists;
	int u8fl, u8fblen, u8cblen, doesexist;
	char *u8c, *u8fs;
	Rune unicodep;
	Font *font = &dc.font;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = { NULL };
	FcCharSet *fccharset;
	uint32_t fg, bg, temp;
	int oneatatime;

	frcflags = FRC_NORMAL;

	/* Fallback on color display for attributes not supported by the font */
	if (base.mode & ATTR_ITALIC && base.mode & ATTR_BOLD) {
		if (dc.ibfont.badslant || dc.ibfont.badweight)
			base.fg = defaultattr;
		font = &dc.ibfont;
		frcflags = FRC_ITALICBOLD;
	} else if (base.mode & ATTR_ITALIC) {
		if (dc.ifont.badslant)
			base.fg = defaultattr;
		font = &dc.ifont;
		frcflags = FRC_ITALIC;
	} else if (base.mode & ATTR_BOLD) {
		if (dc.bfont.badweight)
			base.fg = defaultattr;
		font = &dc.ifont;
		frcflags = FRC_BOLD;
	}

	if (IS_TRUECOL(base.fg)) {
	  #if SELECTION_COLORS_PATCH
	  if ((base.mode & ATTR_SELECTED))
			fg = dc.col[selectionfg] | 0xff000000;
    else
    #endif // SELECTION_COLORS_PATCH
		  fg = base.fg | 0xff000000;
	} else {
	  #if SELECTION_COLORS_PATCH
	  if ((base.mode & ATTR_SELECTED))
			fg = dc.col[selectionfg];
    else
    #endif // SELECTION_COLORS_PATCH
		  fg = dc.col[base.fg];
	}

	if (IS_TRUECOL(base.bg)) {
	  #if SELECTION_COLORS_PATCH
	  if (base.mode & ATTR_SELECTED)
		  bg = dc.col[selectionbg] | 0xff000000;
    else
    #endif // SELECTION_COLORS_PATCH
		  bg = base.bg | 0xff000000;
	} else {
	  #if SELECTION_COLORS_PATCH
	  if (base.mode & ATTR_SELECTED)
		  bg = dc.col[selectionbg];
    else
    #endif // SELECTION_COLORS_PATCH
		  bg = dc.col[base.bg];
	}

	if (base.mode & ATTR_BOLD) {
		/*
		 * change basic system colors [0-7]
		 * to bright system colors [8-15]
		 */
	  #if SELECTION_COLORS_PATCH
		if (BETWEEN(selectionfg, 0, 7) && !(base.mode & ATTR_FAINT))
			fg = dc.col[selectionfg + 8];
    #else
		if (BETWEEN(base.fg, 0, 7) && !(base.mode & ATTR_FAINT))
			fg = dc.col[base.fg + 8];
    #endif // SELECTION_COLORS_PATCH

		if (base.mode & ATTR_ITALIC) {
			font = &dc.ibfont;
			frcflags = FRC_ITALICBOLD;
		} else {
			font = &dc.bfont;
			frcflags = FRC_BOLD;
		}
	}

	if (IS_SET(MODE_REVERSE)) {
		if (fg == dc.col[defaultfg]) {
			fg = dc.col[defaultbg];
		} else {
			fg = ~(fg & 0xffffff);
		}

		if (bg == dc.col[defaultbg]) {
			bg = dc.col[defaultfg];
		} else {
			bg = ~(bg & 0xffffff);
		}
	}

	if (base.mode & ATTR_REVERSE) {
		#if SPOILER_PATCH
		if (bg == fg) {
			bg = dc.col[defaultfg];
			fg = dc.col[defaultbg];
		} else {
			temp = fg;
			fg = bg;
			bg = temp;
		}
		#else
		temp = fg;
		fg = bg;
		bg = temp;
		#endif // SPOILER_PATCH
	}

	if (base.mode & ATTR_FAINT && !(base.mode & ATTR_BOLD)) {
		fg = (fg & (0xff << 24))
			| ((((fg >> 16) & 0xff) / 2) << 16)
			| ((((fg >> 8) & 0xff) / 2) << 8)
			| ((fg & 0xff) / 2);
	}

	if (base.mode & ATTR_BLINK && win.mode & MODE_BLINK)
		fg = bg;

	if (base.mode & ATTR_INVISIBLE)
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	#if ANYSIZE_PATCH
	if (x == 0) {
		wlclear(0, (y == 0)? 0 : winy, win.hborderpx,
				((winy + win.ch >= win.vborderpx + win.th)? win.h : (winy + win.ch)));
	}
	if (winx + width >= win.hborderpx + win.tw) {
		wlclear(winx + width, (y == 0)? 0 : winy, win.w,
				((winy + win.ch >= win.vborderpx + win.th)? win.h : (winy + win.ch)));
	}
	if (y == 0)
		wlclear(winx, 0, winx + width, win.vborderpx);
	if (winy + win.ch >= win.vborderpx + win.th)
		wlclear(winx, winy + win.ch, winx + width, win.h);
	#else
	if (x == 0) {
		wlclear(0, (y == 0)? 0 : winy, borderpx,
				((winy + win.ch >= borderpx + win.th)? win.h : (winy + win.ch)));
	}
	if (winx + width >= borderpx + win.tw) {
		wlclear(winx + width, (y == 0)? 0 : winy, win.w,
				((winy + win.ch >= borderpx + win.th)? win.h : (winy + win.ch)));
	}
	if (y == 0)
		wlclear(winx, 0, winx + width, borderpx);
	if (winy + win.ch >= borderpx + win.th)
		wlclear(winx, winy + win.ch, winx + width, win.h);
  #endif
	/* Clean up the region we want to draw to. */
	wld_fill_rectangle(wld.renderer, (bg & (term_alpha << 24)) | (bg & 0x00FFFFFF), winx, winy, width, win.ch);

	for (xp = winx; bytelen > 0;) {
		/*
		 * Search for the range in the to be printed string of glyphs
		 * that are in the main font. Then print that range. If
		 * some glyph is found that is not in the font, do the
		 * fallback dance.
		 */
		u8fs = s;
		u8fblen = 0;
		u8fl = 0;
		oneatatime = font->width != win.cw;
		for (;;) {
			u8c = s;
			u8cblen = utf8decode(s, &unicodep, UTF_SIZ);
			s += u8cblen;
			bytelen -= u8cblen;

			doesexist = wld_font_ensure_char(font->match, unicodep);
			if (doesexist) {
				u8fl++;
				u8fblen += u8cblen;
				if (!oneatatime && bytelen > 0)
					continue;
			}

			if (u8fl > 0) {
				wld_draw_text(wld.renderer,
						font->match, fg, xp,
						winy + font->ascent,
						u8fs, u8fblen, NULL);
				xp += win.cw * u8fl;
			}
			break;
		}
		if (doesexist) {
			if (oneatatime)
				continue;
			break;
		}

		/* Search the font cache. */
		for (i = 0; i < frclen; i++) {
			charexists = wld_font_ensure_char(frc[i].font, unicodep);
			/* Everything correct. */
			if (charexists && frc[i].flags == frcflags)
				break;
			/* We got a default font for a not found glyph. */
			if (!charexists && frc[i].flags == frcflags \
					&& frc[i].unicodep == unicodep) {
				break;
			}
		}

		/* Nothing was found. */
		if (i >= frclen) {
			if (!font->set)
				font->set = FcFontSort(0, font->pattern,
						1, 0, &fcres);
			fcsets[0] = font->set;

			/*
			 * Nothing was found in the cache. Now use
			 * some dozen of Fontconfig calls to get the
			 * font for one single character.
			 *
			 * Xft and fontconfig are design failures.
			 */
			fcpattern = FcPatternDuplicate(font->pattern);
			fccharset = FcCharSetCreate();

			FcCharSetAddChar(fccharset, unicodep);
			FcPatternAddCharSet(fcpattern, FC_CHARSET,
					fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

			FcConfigSubstitute(0, fcpattern,
					FcMatchPattern);
			FcDefaultSubstitute(fcpattern);

			fontpattern = FcFontSetMatch(0, fcsets, 1,
					fcpattern, &fcres);

			/*
			 * Overwrite or create the new cache entry.
			 */
			if (frclen >= frccap) {
				frccap += 16;
				frc = xrealloc(frc, frccap * sizeof(Fontcache));
			}

			frc[frclen].font = wld_font_open_pattern(wld.fontctx,
					fontpattern);
			frc[frclen].flags = frcflags;
			frc[frclen].unicodep = unicodep;

			i = frclen;
			frclen++;

			FcPatternDestroy(fcpattern);
			FcCharSetDestroy(fccharset);
		}

		wld_draw_text(wld.renderer, frc[i].font, fg,
				xp, winy + frc[i].font->ascent,
				u8c, u8cblen, NULL);

		xp += win.cw * wcwidth(unicodep);
	}

	if (base.mode & ATTR_UNDERLINE) {
		wld_fill_rectangle(wld.renderer, fg, winx, winy + font->ascent + 1,
				width, 1);
	}

	if (base.mode & ATTR_STRUCK) {
		wld_fill_rectangle(wld.renderer, fg, winx, winy + 2 * font->ascent / 3,
				width, 1);
	}

	#if OPENURLONCLICK_PATCH
	/* underline url (openurlonclick patch) */
	if (url_draw && y >= url_y1 && y <= url_y2) {
		int x1 = (y == url_y1) ? url_x1 : 0;
		int x2 = (y == url_y2) ? MIN(url_x2, term.col-1) : url_maxcol;
		if (x + charlen > x1 && x <= x2) {
			int xu = MAX(x, x1);
			int wu = (x2 - xu + 1) * win.cw;
			#if ANYSIZE_PATCH
			xu = win.hborderpx + xu * win.cw;
			#else
			xu = borderpx + xu * win.cw;
			#endif // ANYSIZE_PATCH
			#if VERTCENTER_PATCH
			XftDrawRect(xw.draw, fg, xu, winy + win.cyo + dc.font.ascent * chscale + 2, wu, 1);
			#else
			XftDrawRect(xw.draw, fg, xu, winy + dc.font.ascent * chscale + 2, wu, 1);
			#endif // VERTCENTER_PATCH
			url_draw = (y != url_y2 || x + charlen <= x2);
		}
	}
	#endif // OPENURLONCLICK_PATCH
}

void
wldrawglyph(Glyph g, int x, int y)
{
	static char buf[UTF_SIZ];
	size_t len = utf8encode(g.u, buf);
	int width = g.mode & ATTR_WIDE ? 2 : 1;

	wldraws(buf, g, x, y, width, len);
}

void
wlloadcursor(void)
{
	char *names[] = { mouseshape, "xterm", "ibeam", "text" };
	int i;

	cursor.theme = wl_cursor_theme_load(NULL, 32, wl.shm);

	for (i = 0; !cursor.cursor && i < LEN(names); i++)
		cursor.cursor = wl_cursor_theme_get_cursor(cursor.theme, names[i]);

	cursor.surface = wl_compositor_create_surface(wl.cmp);
}

void
xdrawcursor(int cx, int cy, Glyph g, int ox, int oy, Glyph og)
{
	uint32_t drawcol;

	#if !DYNAMIC_CURSOR_COLOR_PATCH
	/* remove the old cursor */
	if (selected(ox, oy))
		#if SELECTION_COLORS_PATCH
		og.mode |= ATTR_SELECTED;
		#else
		og.mode ^= ATTR_REVERSE;
		#endif // SELECTION_COLORS_PATCH
	#endif // DYNAMIC_CURSOR_COLOR_PATCH

	wldrawglyph(og, ox, oy);

	if (IS_SET(MODE_HIDE))
		return;

	/*
	 * Select the right color for the right mode.
	 */
	g.mode &= ATTR_BOLD|ATTR_ITALIC|ATTR_UNDERLINE|ATTR_STRUCK|ATTR_WIDE
	#if BOXDRAW_PATCH
	|ATTR_BOXDRAW
	#endif // BOXDRAW_PATCH
	#if DYNAMIC_CURSOR_COLOR_PATCH
	|ATTR_REVERSE
	#endif // DYNAMIC_CURSOR_COLOR_PATCH
	#if KEYBOARDSELECT_PATCH && REFLOW_PATCH
	|ATTR_HIGHLIGHT
	#endif // KEYBOARDSELECT_PATCH
	;

	if (ox != cx || oy != cy) {
	  #if ANYSIZE_PATCH
		wl_surface_damage(wl.surface, win.hborderpx + ox * win.cw,
				win.vborderpx + oy * win.ch, win.cw, win.ch);
    #else
		wl_surface_damage(wl.surface, borderpx + ox * win.cw,
				borderpx + oy * win.ch, win.cw, win.ch);
    #endif
	}

	if (IS_SET(MODE_REVERSE)) {
		g.mode |= ATTR_REVERSE;
		g.bg = defaultfg;
		#if SELECTION_COLORS_PATCH
		g.fg = defaultcs;
		drawcol = dc.col[defaultrcs];
		#else
		if (selected(cx, cy)) {
			drawcol = dc.col[defaultcs];
			g.fg = defaultrcs;
		} else {
			drawcol = dc.col[defaultrcs];
			g.fg = defaultcs;
		}
		#endif // SELECTION_COLORS_PATCH
	} else {
		#if SELECTION_COLORS_PATCH
		#if DYNAMIC_CURSOR_COLOR_PATCH
		unsigned int tmpcol = g.bg;
		g.bg = g.fg;
		g.fg = tmpcol;
		#else
		g.fg = defaultbg;
		g.bg = defaultcs;
		#endif // DYNAMIC_CURSOR_COLOR_PATCH
		drawcol = dc.col[defaultcs];
    #else
		if (selected(cx, cy)) {
			#if KEYBOARDSELECT_PATCH && REFLOW_PATCH
			g.mode &= ~(ATTR_REVERSE | ATTR_HIGHLIGHT);
			#elif DYNAMIC_CURSOR_COLOR_PATCH
			g.mode &= ~ATTR_REVERSE;
			#endif // DYNAMIC_CURSOR_COLOR_PATCH
			g.fg = defaultfg;
			g.bg = defaultrcs;
		} else {
			#if DYNAMIC_CURSOR_COLOR_PATCH
			unsigned int tmpcol = g.bg;
			g.bg = g.fg;
			g.fg = tmpcol;
			#else
			g.fg = defaultbg;
			g.bg = defaultcs;
			#endif // DYNAMIC_CURSOR_COLOR_PATCH
		}
		#if DYNAMIC_CURSOR_COLOR_PATCH
		if (IS_TRUECOL(g.bg))
			drawcol = g.bg;
		else
			drawcol = dc.col[g.bg];
		#else
		drawcol = dc.col[g.bg];
		#endif // DYNAMIC_CURSOR_COLOR_PATCH
    #endif // SELECTION_COLORS_PATCH
	}

	#if KEYBOARDSELECT_PATCH && REFLOW_PATCH
	if (g.mode & ATTR_HIGHLIGHT)
		g.mode ^= ATTR_REVERSE;
	#endif // KEYBOARDSELECT_PATCH

	/* draw the new one */
	if (IS_SET(MODE_FOCUSED)) {
		switch (win.cursor) {
		  #if !BLINKING_CURSOR_PATCH
			case 7: /* st-wl extension: snowman */
				g.u = 0x2603;
			/* FALLTHROUGH */
		  #endif // BLINKING_CURSOR_PATCH
			case 0: /* Blinking Block */
			case 1: /* Blinking Block (Default) */
			  #if BLINKING_CURSOR_PATCH
			  if (IS_SET(MODE_BLINK))
				  break;
			  /* FALLTHROUGH */
			  #endif // BLINKING_CURSOR_PATCH
			case 2: /* Steady Block */
				wldrawglyph(g, cx, cy);
				break;
			case 3: /* Blinking Underline */
			  #if BLINKING_CURSOR_PATCH
			  if (IS_SET(MODE_BLINK))
			  	break;
			  /* FALLTHROUGH */
			  #endif // BLINKING_CURSOR_PATCH
			case 4: /* Steady Underline */
			  #if ANYSIZE_PATCH
				wld_fill_rectangle(wld.renderer, drawcol,
						win.hborderpx + cx * win.cw,
						win.vborderpx + (cy + 1) * win.ch - cursorthickness,
						win.cw, cursorthickness);
			  #else
			  wld_fill_rectangle(wld.renderer, drawcol,
					borderpx + cx * win.cw,
					borderpx + (cy + 1) * win.ch - cursorthickness,
					win.cw, cursorthickness);
			  #endif // ANYSIZE_PATCH
				break;
			case 5: /* Blinking bar */
			#if BLINKING_CURSOR_PATCH
			if (IS_SET(MODE_BLINK))
				break;
			/* FALLTHROUGH */
			#endif // BLINKING_CURSOR_PATCH
			case 6: /* Steady bar */
				wld_fill_rectangle(wld.renderer, drawcol,
					  #if ANYSIZE_PATCH
  					win.hborderpx + cx * win.cw,
	  				win.vborderpx + cy * win.ch,
		  			#else
						borderpx + cx * win.cw,
						borderpx + cy * win.ch,
            #endif
						cursorthickness, win.ch);
				break;
		#if BLINKING_CURSOR_PATCH
		case 7: /* Blinking st cursor */
			if (IS_SET(MODE_BLINK))
				break;
			/* FALLTHROUGH */
		case 8: /* Steady st cursor */
			g.u = stcursor;
	    wldrawglyph(g, cx, cy);
			break;
		#endif // BLINKING_CURSOR_PATCH
		}
	} else {
		wld_fill_rectangle(wld.renderer, drawcol,
				#if ANYSIZE_PATCH
				win.hborderpx + cx * win.cw,
				win.vborderpx + cy * win.ch,
				#else
				borderpx + cx * win.cw,
				borderpx + cy * win.ch,
				#endif // ANYSIZE_PATCH
				win.cw - 1, 1);
		wld_fill_rectangle(wld.renderer, drawcol,
				#if ANYSIZE_PATCH
				win.hborderpx + cx * win.cw,
				win.vborderpx + cy * win.ch,
				#else
				borderpx + cx * win.cw,
				borderpx + cy * win.ch,
				#endif // ANYSIZE_PATCH
				1, win.ch - 1);
		wld_fill_rectangle(wld.renderer, drawcol,
				#if ANYSIZE_PATCH
				win.hborderpx + (cx + 1) * win.cw - 1,
				win.vborderpx + cy * win.ch,
				#else
				borderpx + (cx + 1) * win.cw - 1,
				borderpx + cy * win.ch,
				#endif // ANYSIZE_PATCH
				1, win.ch - 1);
		wld_fill_rectangle(wld.renderer, drawcol,
				#if ANYSIZE_PATCH
				win.hborderpx + cx * win.cw,
				win.vborderpx + (cy + 1) * win.ch - 1,
				#else
				borderpx + cx * win.cw,
				borderpx + (cy + 1) * win.ch - 1,
				#endif // ANYSIZE_PATCH
				win.cw, 1);
	}
	#if ANYSIZE_PATCH
	wl_surface_damage(wl.surface, win.hborderpx + cx * win.cw,
			win.vborderpx + cy * win.ch, win.cw, win.ch);
	#else
	wl_surface_damage(wl.surface, borderpx + cx * win.cw,
			borderpx + cy * win.ch, win.cw, win.ch);
	#endif
}

void
regglobal(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0)
  {
		wl.cmp = wl_registry_bind(registry, name, &wl_compositor_interface, 3);
	}
  else if (strcmp(interface, "xdg_wm_base") == 0)
  {
		wl.wm = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wl.wm, &wmlistener, NULL);
	}
  else if (strcmp(interface, "wl_shm") == 0)
  {
		wl.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	}
  else if (strcmp(interface, "wl_seat") == 0)
  {
		wl.seat = wl_registry_bind(registry, name, &wl_seat_interface, 4);
	}
  else if (strcmp(interface, "wl_data_device_manager") == 0)
  {
		wl.datadevmanager = wl_registry_bind(registry, name,
				&wl_data_device_manager_interface, 1);
	}
  else if (strcmp(interface, "wl_output") == 0)
  {
		/* bind to outputs so we can get surface enter events */
		wl_registry_bind(registry, name, &wl_output_interface, 2);
	}
  else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0)
  {
		wl.decoration_manager=wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
  }
}

void
regglobalremove(void *data, struct wl_registry *registry, uint32_t name)
{
}

void
datadevoffer(void *data, struct wl_data_device *datadev,
		struct wl_data_offer *offer)
{
	wl_data_offer_add_listener(offer, &dataofferlistener, NULL);
}

void
datadeventer(void *data, struct wl_data_device *datadev, uint32_t serial,
		struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y,
		struct wl_data_offer *offer)
{
}

void
datadevleave(void *data, struct wl_data_device *datadev)
{
}

void
datadevmotion(void *data, struct wl_data_device *datadev, uint32_t time,
		wl_fixed_t x, wl_fixed_t y)
{
}

void
datadevdrop(void *data, struct wl_data_device *datadev)
{
}

void
datadevselection(void *data, struct wl_data_device *datadev,
		struct wl_data_offer *offer)
{
	if (offer && (uintptr_t) wl_data_offer_get_user_data(offer) == 1)
		wl.seloffer = offer;
	else
		wl.seloffer = NULL;
}

void
dataofferoffer(void *data, struct wl_data_offer *offer, const char *mimetype)
{
	/* mark the offer as usable if it supports plain text */
	if (strncmp(mimetype, "text/plain", 10) == 0)
		wl_data_offer_set_user_data(offer, (void *)(uintptr_t) 1);
}

void
datasrctarget(void *data, struct wl_data_source *source, const char *mimetype)
{
}

void
datasrcsend(void *data, struct wl_data_source *source, const char *mimetype,
		int32_t fd)
{
	char *buf = wlsel.primary;
	int len = strlen(wlsel.primary);
	ssize_t ret;
	while ((ret = write(fd, buf, MIN(len, BUFSIZ))) > 0) {
		len -= ret;
		buf += ret;
	}
	close(fd);
}

void
datasrccancelled(void *data, struct wl_data_source *source)
{
	if (wlsel.source == source) {
		wlsel.source = NULL;
		selclear();
	}
	wl_data_source_destroy(source);
}

void
wlinit(int cols, int rows)
{
	struct wl_registry *registry;

	if (!(wl.dpy = wl_display_connect(NULL)))
		die("Can't open display\n");

	wl.needdraw = true;
  wl.resized = true;
	registry = wl_display_get_registry(wl.dpy);
	wl_registry_add_listener(registry, &reglistener, NULL);
	wld.ctx = wld_wayland_create_context(wl.dpy);
	wld.renderer = wld_create_renderer(wld.ctx);

	wl_display_roundtrip(wl.dpy);

	if (!wl.shm)
		die("Display has no SHM\n");
	if (!wl.seat)
		die("Display has no seat\n");
	if (!wl.datadevmanager)
		die("Display has no data device manager\n");
	if (!wl.wm)
		die("Display has no window manager\n");
	if (!wl.decoration_manager)
		DEBUGPRNT("# Display has no decoration manager\n");

	wl.keyboard = wl_seat_get_keyboard(wl.seat);
	wl_keyboard_add_listener(wl.keyboard, &kbdlistener, NULL);
	wl.pointer = wl_seat_get_pointer(wl.seat);
	wl_pointer_add_listener(wl.pointer, &ptrlistener, NULL);
	wl.datadev = wl_data_device_manager_get_data_device(wl.datadevmanager,
			wl.seat);
	wl_data_device_add_listener(wl.datadev, &datadevlistener, NULL);

	/* font */
	if (!FcInit())
		die("Could not init fontconfig.\n");

	usedfont = (opt_font == NULL)? font : opt_font;
	wld.fontctx = wld_font_create_context();
	wlloadfonts(usedfont, 0);

	xloadcols();
	wlloadcursor();

	/* adjust fixed window geometry */
	#if ANYGEOMETRY_PATCH
	switch (geometry) {
	case CellGeometry:
		#if ANYSIZE_PATCH
		win.w = 2 * win.hborderpx + cols * win.cw;
		win.h = 2 * win.vborderpx + rows * win.ch;
		#else
		win.w = 2 * borderpx + cols * win.cw;
		win.h = 2 * borderpx + rows * win.ch;
		#endif // ANYGEOMETRY_PATCH | ANYSIZE_PATCH
		break;
	case PixelGeometry:
		win.w = cols;
		win.h = rows;
		cols = (win.w - 2 * borderpx) / win.cw;
		rows = (win.h - 2 * borderpx) / win.ch;
		break;
	}
	#elif ANYSIZE_PATCH
	win.w = 2 * win.hborderpx + cols * win.cw;
	win.h = 2 * win.vborderpx + rows * win.ch;
	#else
	win.w = 2 * borderpx + cols * win.cw;
	win.h = 2 * borderpx + rows * win.ch;
	#endif // ANYGEOMETRY_PATCH | ANYSIZE_PATCH

	wl.surface = wl_compositor_create_surface(wl.cmp);
	wl_surface_add_listener(wl.surface, &surflistener, NULL);

	wl.xdgsurface = xdg_wm_base_get_xdg_surface(wl.wm, wl.surface);
	xdg_surface_add_listener(wl.xdgsurface, &xdgsurflistener, NULL);
	wl.xdgtoplevel = xdg_surface_get_toplevel(wl.xdgsurface);
	xdg_toplevel_add_listener(wl.xdgtoplevel, &xdgtoplevellistener, NULL);
	xdg_toplevel_set_app_id(wl.xdgtoplevel, opt_class ? opt_class : termclass);

#if !NO_WINDOW_DECORATIONS_PATCH
  if (wl.decoration_manager)
  {
    struct zxdg_toplevel_decoration_v1 *decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(wl.decoration_manager, wl.xdgtoplevel);
    zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }
#endif

	wl_surface_commit(wl.surface);

	wl.xkb.ctx = xkb_context_new(0);

	win.mode = MODE_NUMLOCK;
	resettitle();

	wlsel.tclick1 = 0;
	wlsel.tclick2 = 0;
	wlsel.primary = NULL;
	wlsel.source = NULL;
}

void
run(void)
{
	fd_set rfd;
	int wlfd = wl_display_get_fd(wl.dpy), blinkset = 0;
	int ttyfd;
  int drawing;
	struct timespec drawtimeout, *tv = NULL, now, lastblink, trigger;
	long timeout;

	/* Look for initial configure. */
	wl_display_roundtrip(wl.dpy);
	ttyfd = ttynew(opt_line, shell, opt_io, opt_cmd);
	cresize(win.w, win.h);

	if (!(IS_SET(MODE_VISIBLE)))
		win.mode |= MODE_VISIBLE;

	clock_gettime(CLOCK_MONOTONIC, &lastblink);

	for (drawing=0, timeout=0;;) {
		FD_ZERO(&rfd);
		FD_SET(ttyfd, &rfd);
		FD_SET(wlfd, &rfd);

		#if SYNC_PATCH
    if (ttyread_pending())
      timeout=0;
    #endif

    if (timeout>=0)
    {
      drawtimeout.tv_nsec = 1E6 * timeout;
      drawtimeout.tv_sec = 0;
      tv = &drawtimeout;
    }
    else
      tv = NULL;

		if (pselect(MAX(wlfd, ttyfd)+1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}

		#if SYNC_PATCH
		int ttyin = FD_ISSET(ttyfd, &rfd) || ttyread_pending();
		if (ttyin)
    #else
		if (FD_ISSET(ttyfd, &rfd))
    #endif
    {
			ttyread();
		}

  	int xev = 0;
    if (FD_ISSET(wlfd, &rfd))
      wl_display_dispatch(wl.dpy);

		clock_gettime(CLOCK_MONOTONIC, &now);

		/*
		 * To reduce flicker and tearing, when new content or event
		 * triggers drawing, we first wait a bit to ensure we got
		 * everything, and if nothing new arrives - we draw.
		 * We start with trying to wait minlatency ms. If more content
		 * arrives sooner, we retry with shorter and shorter periods,
		 * and eventually draw even without idle after maxlatency ms.
		 * Typically this results in low latency while interacting,
		 * maximum latency intervals during `cat huge.txt`, and perfect
		 * sync with periodic updates from animations/key-repeats/etc.
		 */
		#if SYNC_PATCH
		if (ttyin || xev>0)
		#else
		if (FD_ISSET(ttyfd, &rfd))
		#endif // SYNC_PATCH
		{
			if (!drawing) {
				trigger = now;
        drawing=1;
      }
			timeout = (maxlatency - TIMEDIFF(now, trigger));
			if (timeout > 0)
      {
				continue;  /* we have time, try to find idle */
      }
		}

		#if SYNC_PATCH
		if (tinsync(su_timeout)) {
			/*
			 * on synchronized-update draw-suspension: don't reset
			 * drawing so that we draw ASAP once we can (just after
			 * ESU). it won't be too soon because we already can
			 * draw now but we skip. we set timeout > 0 to draw on
			 * SU-timeout even without new content.
			 */
			timeout = minlatency;
			continue;
		}
		#endif // SYNC_PATCH

		timeout = maxlatency;

		#if BLINKING_CURSOR_PATCH
		if (blinktimeout && (cursorblinks || tattrset(ATTR_BLINK)))
		#else
		if (blinktimeout && tattrset(ATTR_BLINK))
		#endif // BLINKING_CURSOR_PATCH
		{
			timeout = blinktimeout - TIMEDIFF(now, lastblink);
			if (timeout <= 0) {
				win.mode ^= MODE_BLINK;
				tsetdirtattr(ATTR_BLINK);
				lastblink = now;
			}
		}

		if (repeat.len > 0) {
			if (TIMEDIFF(now, repeat.last) >= \
					(repeat.started ? keyrepeatinterval : \
					 keyrepeatdelay)) {
				repeat.started = true;
				repeat.last = now;
				ttywrite(repeat.str, repeat.len, 1);
			} else {
				timeout = MIN(timeout, (repeat.started ? \
							keyrepeatinterval : keyrepeatdelay) - \
						TIMEDIFF(now, repeat.last));
			}
		}

		draw();

		wl_display_dispatch_pending(wl.dpy);
		wl_display_flush(wl.dpy);
    drawing = 0;
	}
}

void
usage(void)
{
	die("usage: %s [-aiv] [-c class] [-f font] [-g geometry]"
			" [-n name] [-o file]\n"
			"          [-T title] [-t title] [-w windowid]"
			" [[-e] command [args ...]]\n"
			"       %s [-aiv] [-c class] [-f font] [-g geometry]"
			" [-n name] [-o file]\n"
			"          [-T title] [-t title] [-w windowid] -l line"
			" [stty_args ...]\n", argv0, argv0);
}

int
main(int argc, char *argv[])
{
	#if BLINKING_CURSOR_PATCH
	xsetcursor(cursorstyle);
	#else
	xsetcursor(cursorshape);
	#endif // BLINKING_CURSOR_PATCH

	ARGBEGIN {
		case 'a':
			allowaltscreen = 0;
			break;
	#if ALPHA_PATCH
    case 'A':
      opt_alpha = EARGF(usage());
		break;
	#endif // ALPHA_PATCH
		case 'c':
			opt_class = EARGF(usage());
			break;
		case 'e':
			if (argc > 0)
				--argc, ++argv;
			goto run;
		case 'f':
			opt_font = EARGF(usage());
			break;
		case 'o':
			opt_io = EARGF(usage());
			break;
		case 'l':
			opt_line = EARGF(usage());
			break;
		case 'n':
			opt_name = EARGF(usage());
			break;
		case 't':
		case 'T':
			opt_title = EARGF(usage());
			break;
		case 'w':
			opt_embed = EARGF(usage());
			break;
		case 'v':
			die("%s " VERSION "\n", argv0);
			break;
		default:
			usage();
	} ARGEND;

run:
	if (argc > 0) /* eat all remaining arguments */
		opt_cmd = argv;

	if (!opt_title)
		opt_title = (opt_line || !opt_cmd) ? "st-wl" : opt_cmd[0];

	setlocale(LC_CTYPE, "");
	cols = MAX(cols, 1);
	rows = MAX(rows, 1);
	tnew(cols, rows);
	wlinit(cols, rows);
	selinit();
	run();

	return 0;
}
