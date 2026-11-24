/*
 * What every screen shares: the app's state, the gestures the screens
 * receive, and the common pieces of the interface (header, tab bar,
 * scrolling lists).
 *
 * Screens: home, library, settings (the three tabs), the book, the
 * reading menu over the book, and the book's contents. Each screen is
 * a pair of functions, draw_X and on_X, in its own file.
 */
#ifndef MR_APP_H
#define MR_APP_H

#include <mupdf/fitz.h>
#include "platform.h"
#include "books.h"
#include "dict.h"
#include "draw.h"

#define SCREEN_DPI 160

/* Layout, in pixels. */
#define MARGIN 24
#define HEAD_H 96	/* screen title bar */
#define TAB_H 84	/* Home / Library / Settings along the bottom */
#define TOAST_H 64	/* one-off messages */
#define FOOTER_H 40	/* title and progress under a page */

enum mode {
	MODE_HOME,
	MODE_LIBRARY,
	MODE_SETTINGS,
	MODE_BOOK,
	MODE_MENU,	/* the reading menu, over the page */
	MODE_CONTENTS,
	MODE_DICT,	/* a word's definition, over the page */
	MODE_LOCK,	/* the lock screen: time, battery, slide to open */
	MODE_POWER,	/* "power off / restart / cancel", over a screen */
	MODE_CLOCK,	/* setting the date and time */
	MODE_USB,	/* plugged into a computer: sharing the books */
	MODE_PASS,	/* the passcode keypad */
};

/* What the passcode keypad is for (passcode.c). */
enum { PASS_UNLOCK, PASS_NEW, PASS_CONFIRM, PASS_CHECK, PASS_CHANGE,
       PASS_OFF };

/*
 * What the finger (or a button) meant, worked out once in reader.c
 * from the platform's raw down/move/up so no screen has to.
 */
enum act {
	ACT_TAP,	/* at x, y */
	ACT_DRAG,	/* the finger moved d pixels up (negative: down) */
	ACT_SWIPE,	/* sideways flick: d is +1 (to the left) or -1 */
	ACT_SCROLL,	/* mouse wheel: d pixels down the list */
	ACT_HOME,	/* the n button */
	ACT_NEXT,	/* volume down, arrow keys */
	ACT_PREV,
	ACT_HOLD,	/* finger held still at x, y (HOLD_MS) */
	ACT_RELEASE,	/* finger lifted after a drag that wasn't a swipe */
};

/* How long a still finger takes to become a press and hold. */
#define HOLD_MS 500

/*
 * x, y: where (for a drag, where the finger is now; a->x0, a->y0 is
 * where it started). d: how far, for drags, swipes and scrolls.
 */
struct action {
	enum act type;
	int x, y;
	int d;
};

/*
 * A list drawn once into a picture as tall as the whole list, shown
 * through a window on screen. Scrolling copies a different slice of
 * the picture (a memcpy per row), smooth even on the Nook's CPU.
 */
struct scroller {
	fz_pixmap *pix;
	int scroll;	/* the picture's row at the top of the window */
	int dirty;	/* contents changed: draw the picture again */
};

/* One line of a book's table of contents. */
struct toc_entry {
	char *title;
	fz_location loc;
	int depth;	/* 0 for top-level entries */
};

/* A book cover, shrunk to the size a screen shows it at. */
struct cover {
	const char *path;	/* the mr_book's own string */
	int w, h;
	fz_pixmap *pix;		/* NULL: the book has no cover picture */
};
#define MAX_COVERS 8

struct app {
	fz_context *ctx;
	const mr_platform *plat;
	int w, h;
	fz_pixmap *pix;		/* the one screen buffer */
	mr_library lib;
	const char *books_dir;	/* scanned again after USB sharing */
	enum mode mode;
	char status[200];	/* one-off message, shown on the next frame */

	struct scroller library;
	struct scroller contents;
	struct toc_entry *toc;
	int toc_count;
	int toc_loaded;

	struct cover covers[MAX_COVERS];
	int next_cover;		/* which slot to reuse next */

	/* Settings changed the text layout: re-open the book. */
	int relayout;

	/* The finger, between down and up. */
	int down, dragging;
	int held;		/* already reported as a hold */
	long long down_ms;	/* when it touched (monotonic clock) */
	int x0, y0, last_y;

	/* The dictionary, and the panel showing a word. */
	mr_dict *dict;		/* NULL: no dictionary installed */
	struct scroller dict_view;
	int dict_top, dict_h;	/* where the panel is on screen */
	fz_irect dict_word;	/* the word on the page, highlighted */

	int settings_page;	/* 0 Reading, 1 Device */
	int sort_open;		/* the Library's sort box is showing */

	/*
	 * Awake, dimmed, asleep (system.c). last_input is the last time
	 * anyone touched anything; dimming and sleep count from it.
	 */
	long long last_input;
	int dimmed, asleep;
	int light_before;	/* backlight before dimming or sleep */
	enum mode unlock_to;	/* where unlocking goes back to */
	enum mode menu_over;	/* the screen under the power or USB box */
	long long lock_until;	/* lock screen goes dark again then */
	int slide;		/* the unlock knob, pixels from the left */
	int sliding;
	int power_down;		/* power button held since power_at */
	long long power_at;
	int power_long;		/* that hold already opened the menu */
	int shown_minute;	/* the clock as last drawn */
	int usb;		/* last MR_USB_* seen */
	int usb_sharing;	/* the books are on the computer */
	struct tm clock_edit;	/* the date and time being set */

	/* The passcode keypad: what for, and what's typed so far. */
	int pass_purpose, pass_after;
	char pass_entry[8];
	int pass_len;
	int pass_first;		/* the new code's hash, typed once */
	char pass_msg[64];

	/* Only the script backend: wall time follows its clock. */
	time_t wall0;
	long long now0;

	int quit;		/* leave the main loop */

	/*
	 * The open book. book_path points at the mr_book's own string,
	 * which never moves, so it still finds the book after a sort
	 * has shuffled the array.
	 */
	const char *book_path;
	/*
	 * The book whose reading settings are in force (its own, if it
	 * has them): the open one, or the one being opened. NULL: the
	 * defaults.
	 */
	const char *style_path;
	fz_document *doc;
	fz_location loc;
	int chapters;
	double *starts;		/* chapter weights; NULL = all equal */
};

/* ui.c: pieces every screen uses */
long long app_now(const struct app *a);
time_t app_time(const struct app *a);
void ui_time(struct app *a, time_t t, char *buf, size_t n);
void ui_date_time(struct app *a, time_t t, char *buf, size_t n);
void ui_status(struct app *a, int right, int baseline, int gray);
void ui_header(struct app *a, const char *title);
void ui_header_to(struct app *a, const char *title, int right);
void ui_dialog(struct app *a, const char *title, const char *body,
	       const char **buttons, int n, int on);
int ui_dialog_hit(struct app *a, const char *body, int n, int x, int y);
void draw_screen(struct app *a, enum mode m);
void ui_tabs(struct app *a);
int ui_tab_hit(struct app *a, int x, int y, enum mode *to);
void ui_button(struct app *a, int x, int y, int w, int h,
	       const char *label, int on);
void ui_toast(struct app *a);
void ui_go(struct app *a, enum mode to);
int scroller_by(struct app *a, struct scroller *s, int d, int view_h);
void scroller_show(struct app *a, struct scroller *s, int top, int view_h);

/* book.c */
void text_box(struct app *a, int *x, int *y, int *w, int *h);
mr_book *cur_book(struct app *a);
int open_book(struct app *a, mr_book *b);
void close_book(struct app *a);
void draw_book(struct app *a);
int on_book(struct app *a, const struct action *act);
void draw_menu(struct app *a);
int on_menu(struct app *a, const struct action *act);
void draw_contents(struct app *a);
int on_contents(struct app *a, const struct action *act);

/* lookup.c: the dictionary panel */
int lookup_word(struct app *a, int x, int y);
void close_dict(struct app *a);
void draw_dict(struct app *a);
int on_dict(struct app *a, const struct action *act);

/* system.c: sleep, the lock screen, power, the clock, USB */
int sys_event(struct app *a, const mr_event *ev);
int sys_tick(struct app *a);
int sys_wait_ms(struct app *a);
void sys_sleep(struct app *a);
void sys_update(struct app *a);
void draw_lock(struct app *a);
int on_lock(struct app *a, const struct action *act);
void draw_power(struct app *a);
int on_power(struct app *a, const struct action *act);
void open_clock(struct app *a);
void draw_clock(struct app *a);
int on_clock(struct app *a, const struct action *act);
void draw_usb(struct app *a);
int on_usb(struct app *a, const struct action *act);
void unlock_done(struct app *a);

/* passcode.c */
void pass_unlock(struct app *a);
void pass_settings(struct app *a, int what);
void draw_pass(struct app *a);
int on_pass(struct app *a, const struct action *act);

/* home.c */
void draw_home(struct app *a);
void draw_home_covers(struct app *a);
int on_home(struct app *a, const struct action *act);
void drop_covers(struct app *a);

/* library.c */
void draw_library(struct app *a);
int on_library(struct app *a, const struct action *act);

/* settings.c */
void draw_settings(struct app *a);
int on_settings(struct app *a, const struct action *act);
float setting_font_pt(struct app *a);
int setting_margin_px(struct app *a);
long long setting_dim_ms(const struct app *a);
long long setting_sleep_ms(const struct app *a);
void apply_settings(struct app *a);

#endif
