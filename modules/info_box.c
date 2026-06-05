/* info_box.c -- shared "press a key to wake" overlay for vlock screen savers
 *
 * This program is copyright (C) 2007 Frank Benkstein, and is free
 * software which is freely distributable under the terms of the
 * GNU General Public License version 2, included as the file COPYING in this
 * distribution.  It is NOT public domain software, and any
 * redistribution not permitted by the GNU General Public License is
 * expressly forbidden without prior written permission from
 * the author.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ncurses.h>

#include "info_box.h"

/* Color pair used for the box; chosen above the range the savers use. */
#define INFO_BOX_COLOR_PAIR 8

static int initialized = 0;
static int interval = 0;        /* move interval in seconds; <= 0 means off */
static char message[64];        /* "Press <key> key to wake." */
static chtype box_attr;         /* white-on-black pair, or A_REVERSE fallback */
static int box_x = -1;          /* current top-left; < 0 until first placement */
static int box_y = -1;
static time_t last_move = 0;

/* Human-readable wake-key name, mirroring vlock-main's VLOCK_WAKE_KEY values. */
static const char *wake_key_name(void)
{
    const char *k = getenv("VLOCK_WAKE_KEY");

    if (k == NULL || strcmp(k, "any") == 0)
        return "any";
    else if (strcmp(k, "enter") == 0 || strcmp(k, "return") == 0)
        return "enter";
    else if (strcmp(k, "space") == 0)
        return "space";
    else if (strcmp(k, "backspace") == 0)
        return "backspace";
    else
        return "any";
}

static void info_box_init(void)
{
    const char *iv = getenv("VLOCK_INFO_BOX");

    initialized = 1;
    interval = (iv != NULL) ? atoi(iv) : 0;

    if (interval <= 0)
        return;

    snprintf(message, sizeof message, "Press %s key to wake.", wake_key_name());

    /* Prefer a real white-on-black pair so the box has a solid black
     * background; fall back to reverse video if color is unavailable. */
    box_attr = A_REVERSE;

    if (has_colors()) {
        start_color();

        if (INFO_BOX_COLOR_PAIR < COLOR_PAIRS
            && init_pair(INFO_BOX_COLOR_PAIR, COLOR_WHITE, COLOR_BLACK) == OK)
            box_attr = COLOR_PAIR(INFO_BOX_COLOR_PAIR);
    }
}

/* Blank a rectangle with default attributes, so a moved box leaves no ghost on
 * savers that do not repaint the whole screen each frame (e.g. train). */
static void clear_rect(int x, int y, int w, int h)
{
    attrset(A_NORMAL);

    for (int r = 0; r < h && y + r < LINES; r++) {
        move(y + r, x);

        for (int c = 0; c < w && x + c < COLS; c++)
            addch(' ');
    }
}

void info_box_draw(void)
{
    int bw, bh;
    time_t now;

    if (!initialized)
        info_box_init();

    if (interval <= 0)
        return;

    bw = (int) strlen(message) + 2;  /* one column of padding on each side */
    bh = 3;                          /* one blank row above and below */

    /* Skip if the box would not fit on the current screen. */
    if (bw > COLS || bh > LINES)
        return;

    now = time(NULL);

    if (box_x < 0 || (now - last_move) >= interval) {
        /* Erase the old footprint before relocating. */
        if (box_x >= 0)
            clear_rect(box_x, box_y, bw, bh);

        box_x = rand() % (COLS - bw + 1);
        box_y = rand() % (LINES - bh + 1);
        last_move = now;
    }

    /* Keep the box on screen if the terminal shrank since the last move. */
    if (box_x > COLS - bw)
        box_x = COLS - bw;
    if (box_y > LINES - bh)
        box_y = LINES - bh;

    attron(box_attr);

    move(box_y, box_x);
    for (int c = 0; c < bw; c++)
        addch(' ');

    move(box_y + 1, box_x);
    addch(' ');
    addstr(message);
    addch(' ');

    move(box_y + 2, box_x);
    for (int c = 0; c < bw; c++)
        addch(' ');

    attroff(box_attr);
}
