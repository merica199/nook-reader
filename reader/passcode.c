/*
 * The passcode: an optional 4 to 6 digit code asked for after the
 * lock screen's slider. One keypad screen serves every use:
 *
 *   PASS_UNLOCK    after sliding to unlock
 *   PASS_NEW       setting a passcode: type it ...
 *   PASS_CONFIRM   ... and type it again
 *   PASS_CHECK     changing it or turning it off: the current one
 *                  first
 *
 * Only a hash of the code is kept (the "pass" setting), never the
 * digits. It keeps casual hands out; it isn't encryption: anyone who
 * can start CyanogenMod with adb can take the line out of the state
 * file.
 */
#include <stdio.h>
#include <string.h>
#include "app.h"

#define MIN_DIGITS 4
#define MAX_DIGITS 6

/* The keypad: 1-9, then Delete, 0, OK. */
#define KEY_W 150
#define KEY_H 104
#define KEY_GAP 20
#define KEYS_TOP 400

static const char *key_labels[12] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "Delete", "0", "OK",
};

/*
 * FNV-1a over the digits with a fixed prefix, so the state file holds
 * a number, not the code. Never 0: 0 means "no passcode".
 */
static int pass_hash(const char *digits)
{
	unsigned h = 2166136261u;
	const char *s = "mynook:";
	for (int pass = 0; pass < 2; pass++, s = digits)
		for (; *s; s++) {
			h ^= (unsigned char)*s;
			h *= 16777619u;
		}
	return h ? (int)h : 1;
}

static void start(struct app *a, int purpose, const char *msg)
{
	a->pass_purpose = purpose;
	a->pass_len = 0;
	a->pass_entry[0] = '\0';
	snprintf(a->pass_msg, sizeof a->pass_msg, "%s", msg);
	a->mode = MODE_PASS;
}

void pass_unlock(struct app *a)
{
	start(a, PASS_UNLOCK, "Enter your passcode");
}

/* From Settings: what = PASS_NEW (set), PASS_CHANGE or PASS_OFF. */
void pass_settings(struct app *a, int what)
{
	if (what == PASS_NEW) {
		start(a, PASS_NEW, "Choose a passcode: 4 to 6 digits");
		return;
	}
	a->pass_after = what;
	start(a, PASS_CHECK, "Enter your current passcode");
}

static void key_box(int i, int *x, int *y)
{
	int x0 = (MR_NOOK_W - 3 * KEY_W - 2 * KEY_GAP) / 2;
	*x = x0 + (i % 3) * (KEY_W + KEY_GAP);
	*y = KEYS_TOP + (i / 3) * (KEY_H + KEY_GAP);
}

/* "Cancel" in the header, everywhere but unlocking. */
static int cancel_hit(struct app *a, int x, int y)
{
	return a->pass_purpose != PASS_UNLOCK && y < HEAD_H &&
	       x > a->w - 160;
}

void draw_pass(struct app *a)
{
	fz_context *ctx = a->ctx;
	fz_clear_pixmap_with_value(ctx, a->pix, MR_WHITE);

	int unlocking = a->pass_purpose == PASS_UNLOCK;
	ui_header_to(a, "Passcode",
		     unlocking ? a->w - MARGIN : a->w - MARGIN - 126);
	if (!unlocking)
		ui_button(a, a->w - MARGIN - 110, 22, 110, 52, "Cancel", 0);

	mr_text(ctx, a->pix, a->w / 2, HEAD_H + 90, a->pass_msg, MR_BOLD,
		26, MR_BLACK, a->w - 2 * MARGIN, MR_CENTER);

	/* one box per digit typed, filled in */
	int box = 28, gap = 18;
	int shown = a->pass_len > MIN_DIGITS ? a->pass_len : MIN_DIGITS;
	int x0 = (a->w - shown * box - (shown - 1) * gap) / 2;
	for (int i = 0; i < shown; i++) {
		int x = x0 + i * (box + gap);
		if (i < a->pass_len)
			mr_fill(ctx, a->pix, x, HEAD_H + 170, box, box,
				MR_BLACK);
		else
			mr_outline(ctx, a->pix, x, HEAD_H + 170, box, box,
				   2, MR_GRAY);
	}

	for (int i = 0; i < 12; i++) {
		int x, y;
		key_box(i, &x, &y);
		ui_button(a, x, y, KEY_W, KEY_H, key_labels[i], i == 11);
	}
}

/* OK was tapped with the digits in a->pass_entry. */
static int submit(struct app *a)
{
	int h = pass_hash(a->pass_entry);
	int n = a->pass_len;

	switch (a->pass_purpose) {
	case PASS_UNLOCK:
		if (h == a->lib.set.pass) {
			unlock_done(a);
			return 1;
		}
		start(a, PASS_UNLOCK, "Wrong passcode. Try again");
		return 1;

	case PASS_NEW:
		if (n < MIN_DIGITS) {
			start(a, PASS_NEW, "At least 4 digits, please");
			return 1;
		}
		a->pass_first = h;
		start(a, PASS_CONFIRM, "Enter it again");
		return 1;

	case PASS_CONFIRM:
		if (h != a->pass_first) {
			start(a, PASS_NEW, "They didn't match. Start again");
			return 1;
		}
		a->lib.set.pass = h;
		mr_library_save(&a->lib);
		snprintf(a->status, sizeof a->status, "Passcode set");
		a->mode = MODE_SETTINGS;
		return 1;

	case PASS_CHECK:
		if (h != a->lib.set.pass) {
			start(a, PASS_CHECK, "Wrong passcode. Try again");
			return 1;
		}
		if (a->pass_after == PASS_OFF) {
			a->lib.set.pass = 0;
			mr_library_save(&a->lib);
			snprintf(a->status, sizeof a->status,
				 "Passcode turned off");
			a->mode = MODE_SETTINGS;
			return 1;
		}
		start(a, PASS_NEW, "Choose a new passcode: 4 to 6 digits");
		return 1;
	}
	return 0;
}

int on_pass(struct app *a, const struct action *act)
{
	if (act->type != ACT_TAP)
		return 0;
	if (cancel_hit(a, act->x, act->y)) {
		a->mode = MODE_SETTINGS;
		return 1;
	}
	for (int i = 0; i < 12; i++) {
		int x, y;
		key_box(i, &x, &y);
		if (act->x < x || act->x >= x + KEY_W || act->y < y ||
		    act->y >= y + KEY_H)
			continue;
		if (i == 9) {		/* Delete */
			if (a->pass_len > 0)
				a->pass_entry[--a->pass_len] = '\0';
			return 1;
		}
		if (i == 11)		/* OK */
			return submit(a);
		if (a->pass_len < MAX_DIGITS) {
			a->pass_entry[a->pass_len++] = key_labels[i][0];
			a->pass_entry[a->pass_len] = '\0';
		}
		return 1;
	}
	return 0;
}
