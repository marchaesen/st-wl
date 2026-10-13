/* See LICENSE for license details. */

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <fontconfig/fontconfig.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <pixman.h>
#include "patches.h"

/* Arbitrary sizes */
#define UTF_SIZ       4
#define ESC_BUF_SIZ   (128*UTF_SIZ)


/* macros */
#ifdef DEBUG
#   define DEBUGPRNT(format, ...) \
        fprintf(stderr, "# %s: " format, __func__, ## __VA_ARGS__)
#else
#   define DEBUGPRNT(format, ...)
#endif

#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
/*
 * Bits that are pure internal bookkeeping (line-wrap continuation,
 * ligature linkage, the undercurl "did the underline style/color
 * change" toggle) rather than a visible difference in the glyph -
 * excluded from ATTRCMP so a no-op rewrite of unchanged content
 * doesn't look like a real change.
 */
#if LIGATURES_PATCH && UNDERCURL_PATCH
#define ATTR_IGNOREMASK		(ATTR_WRAP | ATTR_LIGA | ATTR_DIRTYUNDERLINE)
#elif LIGATURES_PATCH
#define ATTR_IGNOREMASK		(ATTR_WRAP | ATTR_LIGA)
#elif UNDERCURL_PATCH
#define ATTR_IGNOREMASK		(ATTR_WRAP | ATTR_DIRTYUNDERLINE)
#else
#define ATTR_IGNOREMASK		(ATTR_WRAP)
#endif // LIGATURES_PATCH | UNDERCURL_PATCH
#if KITTY_GRAPHICS_PATCH
#define ATTRCMP(a, b)		(((a).mode & ~ATTR_IGNOREMASK) != ((b).mode & ~ATTR_IGNOREMASK) || \
				(a).fg != (b).fg || \
				(a).bg != (b).bg || \
				(a).decor != (b).decor)
#else
#define ATTRCMP(a, b)		(((a).mode & ~ATTR_IGNOREMASK) != ((b).mode & ~ATTR_IGNOREMASK) || \
				(a).fg != (b).fg || \
				(a).bg != (b).bg)
#endif // KITTY_GRAPHICS_PATCH
#define TIMEDIFF(t1, t2)	((t1.tv_sec-t2.tv_sec)*1000 + \
				(t1.tv_nsec-t2.tv_nsec)/1000000)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define TRUECOLOR(r,g,b)	(1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)		(1 << 24 & (x))
#if KITTY_GRAPHICS_PATCH
/* This decor color indicates that the fg color should be used. Note that it's
 * not a 24-bit color because the 25-th bit is not set. */
#define DECOR_DEFAULT_COLOR	0x0ffffff
#endif // KITTY_GRAPHICS_PATCH
#if SCROLLBACK_PATCH || REFLOW_PATCH
#define HISTSIZE      2000
#endif // SCROLLBACK_PATCH | REFLOW_PATCH

#if DRAG_AND_DROP_PATCH
#define HEX_TO_INT(c)		((c) >= '0' && (c) <= '9' ? (c) - '0' : \
				(c) >= 'a' && (c) <= 'f' ? (c) - 'a' + 10 : \
				(c) >= 'A' && (c) <= 'F' ? (c) - 'A' + 10 : -1)
#endif // DRAG_AND_DROP_PATCH

enum glyph_attribute {
	ATTR_NULL           = 0,
	ATTR_SET            = 1 << 0,
	ATTR_BOLD           = 1 << 1,
	ATTR_FAINT          = 1 << 2,
	ATTR_ITALIC         = 1 << 3,
	ATTR_UNDERLINE      = 1 << 4,
	ATTR_BLINK          = 1 << 5,
	ATTR_REVERSE        = 1 << 6,
	ATTR_INVISIBLE      = 1 << 7,
	ATTR_STRUCK         = 1 << 8,
	ATTR_WRAP           = 1 << 9,
	ATTR_WIDE           = 1 << 10,
	ATTR_WDUMMY         = 1 << 11,
	#if SELECTION_COLORS_PATCH
	ATTR_SELECTED       = 1 << 12,
	#endif // SELECTION_COLORS_PATCH | REFLOW_PATCH
	#if BOXDRAW_PATCH
	ATTR_BOXDRAW        = 1 << 13,
	#endif // BOXDRAW_PATCH
	#if UNDERCURL_PATCH
	ATTR_DIRTYUNDERLINE = 1 << 14,
	#endif // UNDERCURL_PATCH
	#if LIGATURES_PATCH
	ATTR_LIGA           = 1 << 15,
	#endif // LIGATURES_PATCH
	#if SIXEL_PATCH
	ATTR_SIXEL          = 1 << 16,
	#endif // SIXEL_PATCH
	#if KEYBOARDSELECT_PATCH && REFLOW_PATCH
	ATTR_HIGHLIGHT      = 1 << 17,
	#endif // KEYBOARDSELECT_PATCH
	ATTR_BOLD_FAINT = ATTR_BOLD | ATTR_FAINT,
	#if OSC133_PATCH
	ATTR_FTCS_PROMPT    = 1 << 18,  /* OSC 133 ; A ST */
	#endif // OSC133_PATCH
	#if KITTY_GRAPHICS_PATCH
	ATTR_IMAGE          = 1 << 19,  /* kitty graphics image placeholder */
	#endif // KITTY_GRAPHICS_PATCH
};

#if SIXEL_PATCH
typedef struct _ImageList {
	struct _ImageList *next, *prev;
	uint32_t *pixels;
	pixman_image_t *pixmap;
	pixman_image_t *clipmask;
	int width;
	int height;
	int x;
	int y;
	#if REFLOW_PATCH
	int reflow_y;
	#endif // REFLOW_PATCH
	int cols;
	int cw;
	int ch;
	int transparent;
} ImageList;
#endif // SIXEL_PATCH

#if WIDE_GLYPHS_PATCH
enum drawing_mode {
	DRAW_NONE = 0,
	DRAW_BG   = 1 << 0,
	DRAW_FG   = 1 << 1,
};
#endif // WIDE_GLYPHS_PATCH

/* Used to control which screen(s) keybindings and mouse shortcuts apply to. */
enum screen {
	S_PRI = -1, /* primary screen */
	S_ALL = 0,  /* both primary and alt screen */
	S_ALT = 1   /* alternate screen */
};

enum selection_mode {
	SEL_IDLE = 0,
	SEL_EMPTY = 1,
	SEL_READY = 2
};

enum selection_type {
	SEL_REGULAR = 1,
	SEL_RECTANGULAR = 2
};

enum selection_snap {
	SNAP_WORD = 1,
	SNAP_LINE = 2
};

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

typedef uint_least32_t Rune;

typedef struct {
	Rune u;           /* character code */
	uint32_t mode;    /* attribute flags */
	uint32_t fg;      /* foreground  */
	uint32_t bg;      /* background  */
	#if UNDERCURL_PATCH
	int ustyle;	      /* underline style */
	int ucolor[3];    /* underline color */
	#endif // UNDERCURL_PATCH
	#if KITTY_GRAPHICS_PATCH
	uint32_t decor;   /* decoration color (SGR 58), image placement id */
	#endif // KITTY_GRAPHICS_PATCH
} Glyph;

typedef Glyph *Line;

#if LIGATURES_PATCH
typedef struct {
	int ox;
	int charlen;
	int numspecs;
	Glyph base;
} GlyphFontSeq;
#endif // LIGATURES_PATCH

typedef struct {
	Glyph attr; /* current char attributes */
	int x;
	int y;
	char state;
} TCursor;

/* Internal representation of the screen */
typedef struct {
	int row;      /* nb row */
	int col;      /* nb col */
	#if COLUMNS_PATCH
	int maxcol;
	#endif // COLUMNS_PATCH
	Line *line;   /* screen */
	Line *alt;    /* alternate screen */
	#if REFLOW_PATCH
	Line hist[HISTSIZE]; /* history buffer */
	int histi;           /* history index */
	int histf;           /* nb history available */
	int scr;             /* scroll back */
	int wrapcwidth[2];   /* used in updating WRAPNEXT when resizing */
	#elif SCROLLBACK_PATCH
	Line hist[HISTSIZE]; /* history buffer */
	int histi;    /* history index */
	int histn;    /* number of history entries */
	int scr;      /* scroll back */
	#endif // SCROLLBACK_PATCH | REFLOW_PATCH
	int *dirty;   /* dirtyness of lines */
	TCursor c;    /* cursor */
	int ocx;      /* old cursor col */
	int ocy;      /* old cursor row */
	int top;      /* top    scroll limit */
	int bot;      /* bottom scroll limit */
	int mode;     /* terminal mode flags */
	int esc;      /* escape state flags */
	char trantbl[4]; /* charset table translation */
	int charset;  /* current charset */
	int icharset; /* selected charset for sequence */
	int *tabs;
	#if SIXEL_PATCH
	ImageList *images;     /* sixel images */
	ImageList *images_alt; /* sixel images for alternate screen */
	#endif // SIXEL_PATCH
	Rune lastc;   /* last printed char outside of sequence, 0 if control */
	#if OSC7_PATCH
	char* cwd;    /* current working directory */
	#endif // OSC7_PATCH
} Term;

typedef union {
	int i;
	uint ui;
	float f;
	const void *v;
	const char *s;
} Arg;

/* Purely graphic info */
typedef struct {
	int tw, th; /* tty width and height */
	int w, h; /* window width and height */
	#if BACKGROUND_IMAGE_PATCH
	int x, y; /* window location */
	#endif // BACKGROUND_IMAGE_PATCH
	#if ANYSIZE_PATCH
	int hborderpx, vborderpx;
	#endif // ANYSIZE_PATCH
	int ch; /* char height */
	int cw; /* char width  */
	#if VERTCENTER_PATCH
	int cyo; /* char y offset */
	#endif // VERTCENTER_PATCH
	int mode; /* window state/mode flags */
	int cursor; /* cursor style */
} TermWindow;

/* types used in config.h */
typedef struct {
	uint mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Shortcut;

typedef struct {
	xkb_keysym_t k;
	uint mask;
	const char *s;
	/* three valued logic variables: 0 indifferent, 1 on, -1 off */
	signed char appkey;    /* application keypad */
	signed char appcursor; /* application cursor */
} Key;

/* Font structure */
#define Font Font_
typedef struct {
	int height;
	int width;
	int ascent;
	int descent;
	int badslant;
	int badweight;
	short lbearing;
	short rbearing;
	struct wld_font *match;
	FcFontSet *set;
	FcPattern *pattern;
} Font;

/* Drawing Context */
typedef struct {
	uint32_t *col;
	size_t collen;
	Font font, bfont, ifont, ibfont;
} DC;

void die(const char *, ...);
void redraw(void);
void draw(void);
void drawregion(int, int, int, int);
void tfulldirt(void);

void printscreen(const Arg *);
void printsel(const Arg *);
void sendbreak(const Arg *);
void toggleprinter(const Arg *);

int tattrset(int);
int tisaltscr(void);
void tnew(int, int);
void tresize(int, int);
void tsetdirtattr(int);
void ttyhangup(void);
int ttynew(const char *, char *, const char *, char **);
size_t ttyread(void);
void ttyresize(int, int);
void ttywrite(const char *, size_t, int);

void resettitle(void);

void selclear(void);
void selinit(void);
void selstart(int, int, int);
void selextend(int, int, int, int);
int selected(int, int);
char *getsel(void);

#if KITTY_GRAPHICS_PATCH
Glyph getglyphat(int, int);
#endif // KITTY_GRAPHICS_PATCH

size_t utf8decode(const char *, Rune *, size_t);
size_t utf8encode(Rune, char *);

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);

int xgetcolor(int x, unsigned char *r, unsigned char *g, unsigned char *b);

#if BOXDRAW_PATCH
int isboxdraw(Rune);
ushort boxdrawindex(const Glyph *);
#ifdef XFT_VERSION
/* only exposed to x.c, otherwise we'll need Xft.h for the types */
void boxdraw_xinit(Display *, Colormap, XftDraw *, Visual *);
void drawboxes(int, int, int, int, XftColor *, XftColor *, const XftGlyphFontSpec *, int);
#endif // XFT_VERSION
#endif // BOXDRAW_PATCH

/* config.h globals */
extern char *utmp;
extern char *scroll;
extern char *stty_args;
extern char *vtiden;
extern wchar_t *worddelimiters;
extern wchar_t *snap_line_delimiters;
#if KEYBOARDSELECT_PATCH && REFLOW_PATCH
extern wchar_t *kbds_sdelim;
extern wchar_t *kbds_ldelim;
#endif // KEYBOARDSELECT_PATCH
extern int allowaltscreen;
extern int allowwindowops;
extern char *termname;
extern unsigned int tabspaces;
extern char *termclass;
extern unsigned int defaultfg;
extern unsigned int defaultbg;
extern unsigned int defaultcs;
#if EXTERNALPIPE_PATCH
extern int extpipeactive;
#endif // EXTERNALPIPE_PATCH

#if BOXDRAW_PATCH
extern const int boxdraw, boxdraw_bold, boxdraw_braille;
#endif // BOXDRAW_PATCH
#if ALPHA_PATCH
extern float alpha;
#if ALPHA_FOCUS_HIGHLIGHT_PATCH
extern float alphaUnfocused;
#endif // ALPHA_FOCUS_HIGHLIGHT_PATCH
#endif // ALPHA_PATCH

extern DC dc;
extern TermWindow win;
extern Term term;

#if KITTY_GRAPHICS_PATCH
/* Accessors to the decoration color stored in `decor`.
 * The 25-th bit is used to indicate if it's a 24-bit color. */
static inline uint32_t tgetdecorcolor(Glyph *g) { return g->decor & 0x1ffffff; }
static inline void tsetdecorcolor(Glyph *g, uint32_t color) {
	g->decor = (g->decor & ~0x1ffffff) | (color & 0x1ffffff);
}

/* Some accessors to image placeholder properties stored in `u`:
 * - row (1-base) - 9 bits
 * - column (1-base) - 9 bits
 * - most significant byte of the image id plus 1 - 9 bits (0 means unspecified,
 *   don't forget to subtract 1).
 * - the original number of diacritics (0, 1, 2, or 3) - 2 bits
 * - whether this is a classic (1) or Unicode (0) placeholder - 1 bit */
static inline uint32_t tgetimgrow(Glyph *g) { return g->u & 0x1ff; }
static inline uint32_t tgetimgcol(Glyph *g) { return (g->u >> 9) & 0x1ff; }
static inline uint32_t tgetimgid4thbyteplus1(Glyph *g) { return (g->u >> 18) & 0x1ff; }
static inline uint32_t tgetimgdiacriticcount(Glyph *g) { return (g->u >> 27) & 0x3; }
static inline uint32_t tgetisclassicplaceholder(Glyph *g) { return (g->u >> 29) & 0x1; }
static inline void tsetimgrow(Glyph *g, uint32_t row) {
	g->u = (g->u & ~0x1ff) | (row & 0x1ff);
}
static inline void tsetimgcol(Glyph *g, uint32_t col) {
	g->u = (g->u & ~(0x1ff << 9)) | ((col & 0x1ff) << 9);
}
static inline void tsetimg4thbyteplus1(Glyph *g, uint32_t byteplus1) {
	g->u = (g->u & ~(0x1ff << 18)) | ((byteplus1 & 0x1ff) << 18);
}
static inline void tsetimgdiacriticcount(Glyph *g, uint32_t count) {
	g->u = (g->u & ~(0x3 << 27)) | ((count & 0x3) << 27);
}
static inline void tsetisclassicplaceholder(Glyph *g, uint32_t isclassic) {
	g->u = (g->u & ~(0x1 << 29)) | ((isclassic & 0x1) << 29);
}

/* Returns the full image id. This is a naive implementation, if the most
 * significant byte is not specified, it's assumed to be 0 instead of inferring
 * it from the cells to the left. */
static inline uint32_t tgetimgid(Glyph *g) {
	uint32_t msb = tgetimgid4thbyteplus1(g);
	if (msb != 0)
		--msb;
	return (msb << 24) | (g->fg & 0xFFFFFF);
}

/* Sets the full image id. */
static inline void tsetimgid(Glyph *g, uint32_t id) {
	g->fg = (id & 0xFFFFFF) | (1 << 24);
	tsetimg4thbyteplus1(g, ((id >> 24) & 0xFF) + 1);
}

static inline uint32_t tgetimgplacementid(Glyph *g) {
	if (tgetdecorcolor(g) == DECOR_DEFAULT_COLOR)
		return 0;
	return g->decor & 0xFFFFFF;
}

static inline void tsetimgplacementid(Glyph *g, uint32_t id) {
	g->decor = (id & 0xFFFFFF) | (1 << 24);
}
#endif // KITTY_GRAPHICS_PATCH
