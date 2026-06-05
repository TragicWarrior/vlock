/* wetpipes.c -- a screen saving plugin for vlock.
 *
 * Renders a static "Super Mario"-style scene: a layer of small bricks
 * (brown with gray mortar) along the bottom of the screen, with a row
 * of pipes (green or gray) rising out of them.  Future revisions will
 * add animated sprites on top -- the renderer is laid out around a
 * simple sprite_t type so that can drop in without restructuring.
 *
 * Copyright (C) 2026 Bryan Christ <bryan.christ@gmail.com>
 * Released under the WTFPL, like the other vlock modules.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <ncurses.h>

#include "process.h"
#include "vlock_plugin.h"
#include "info_box.h"

/* ── plugin dependencies ────────────────────────────────────────── */

/* No `depends` array: declaring depends = { "all", NULL } would have
 * vlock's resolver silently drop us when the user runs `vlock wetpipes`
 * without also passing -a (since "all" isn't loaded then).  cmatrix and
 * train both omit it for the same reason -- a screen saver doesn't
 * actually need the lock-all-VTs plugin. */

/* ── color pairs ────────────────────────────────────────────────── */

#define PAIR_BRICK          1   /* mortar fg on brown bg */
#define PAIR_GREEN_MAIN     2   /* solid green bg */
#define PAIR_GREEN_LIGHT    3   /* solid bright-green bg */
#define PAIR_GREEN_JOIN     4   /* bright-green fg on green bg (for '_') */
#define PAIR_GRAY_MAIN      5   /* solid bright-black bg (dark gray) */
#define PAIR_GRAY_LIGHT     6   /* solid white bg (light gray) */
#define PAIR_GRAY_JOIN      7   /* white fg on bright-black bg (for '_') */
/* info_box.c claims pair 8, so skip it */
#define PAIR_BG             9   /* solid blue canvas */
#define PAIR_BG_SKY        10   /* solid bright-blue canvas (top strip) */
#define PAIR_BUBBLE        11   /* bright white fg on blue bg */
#define PAIR_BUBBLE_BLUE   12   /* bright blue fg on blue bg (light blue bubbles) */
#define SKY_FRACTION       10   /* percent of LINES for the sky strip */

/* ── pipe shape ─────────────────────────────────────────────────── */

#define PIPE_KIND_GREEN     0
#define PIPE_KIND_GRAY      1

#define MAX_STREAMS_PER_PIPE   12       /* also = max concurrent bubbles
                                           per pipe -- one bubble per
                                           stream, streams hold distinct
                                           columns and don't share */
#define BUBBLE_KIND_COUNT       5

typedef enum {
    BUB_COLON  = 0,     /* ':' smallest, fastest */
    BUB_DOT    = 1,     /* '.' next, 2nd fastest -- may grow to 'o' */
    BUB_SMALL  = 2,     /* 'o' 3rd fastest -- may grow to '8' or 'O' */
    BUB_LARGE  = 3,     /* '8' slower */
    BUB_FAT    = 4      /* 'O' slowest */
} bubble_kind_t;

static const char bubble_glyph[BUBBLE_KIND_COUNT]  = { ':', '.', 'o', '8', 'O' };
/* frames per upward move -- smaller = faster.  '.' is now the fastest
   (every bubble starts as '.'); the others grow from it and are
   progressively slower. */
static const int  bubble_period[BUBBLE_KIND_COUNT] = {  3,   2,   4,   5,   7  };

/* delay between consecutive state transitions so the change is
   readable -- ~500ms at napms(80) is 6-7 frames */
#define TRANSITION_LOCKOUT_FRAMES   6

/* A stream can hold up to one bubble of each color simultaneously
   (so two bubbles in the same column, never the same color). */
#define BUBBLE_COLOR_COUNT          2
typedef enum {
    BUB_COLOR_WHITE = 0,
    BUB_COLOR_BLUE  = 1,
} bubble_color_t;

typedef struct {
    int             x;
    int             y;
    bubble_kind_t   kind;
    bubble_color_t  color;
    int             move_counter;       /* used during float phase */
    int             transition_lockout; /* used during pipe phase */
    bool            floating;           /* false = on pipe, true = rising */
    bool            alive;
} bubble_t;

typedef struct {
    int         x;          /* left edge of the flange (screen col) */
    int         y_top;      /* top row of the pipe (the flange) */
    int         shaft_w;    /* shaft width in cells (even number) */
    int         height;     /* total pipe height (flange + join + body) */
    int         kind;       /* PIPE_KIND_* */

    /* Streams hold distinct columns.  bubbles[stream][color] gives at
       most one alive bubble per (stream, color) pair -- so a column
       can have two bubbles in flight as long as they're different
       colors. */
    int         stream_count;
    int         stream_x[MAX_STREAMS_PER_PIPE];
    bubble_t    bubbles[MAX_STREAMS_PER_PIPE][BUBBLE_COLOR_COUNT];
    int         next_emit_frame;
} pipe_t;

/* ── sprite (placeholder for future use) ────────────────────────── */

typedef struct {
    int             x, y;
    int             width, height;
    /* width*height entries, row-major.  data[i] == ' ' means transparent
       (skip the cell, leaving the underlying pipe/brick visible). */
    const char     *data;
    short           pair;
    int             attrs;
} sprite_t;

__attribute__((unused))
static void
draw_sprite(const sprite_t *s)
{
    int y, x;

    if(s == NULL || s->data == NULL) return;

    attron(COLOR_PAIR(s->pair) | s->attrs);
    for(y = 0; y < s->height; y++)
    {
        for(x = 0; x < s->width; x++)
        {
            char ch = s->data[y * s->width + x];
            if(ch == ' ') continue;     /* transparent */
            mvaddch(s->y + y, s->x + x, ch);
        }
    }
    attroff(COLOR_PAIR(s->pair) | s->attrs);
}

/* ── globals ────────────────────────────────────────────────────── */

#define BRICK_ROWS              2       /* one complete BTEE/TTEE tile
                                           = one visible layer of bricks */
#define PIPE_HEIGHT_FRACTION    30      /* percent of LINES */
#define MIN_PIPE_HEIGHT         4       /* 2 flange rows + min 2 shaft rows */
#define FLANGE_ROWS             2
#define DEFAULT_SHAFT_W         13      /* uniform across all pipes;
                                           odd is fine -- the underline
                                           separator runs the full width
                                           so no even-split required */
#define MIN_PIPE_GAP            3       /* cells between adjacent flanges */
#define MIN_PIPES               2
#define MAX_PIPES               3

static pipe_t              *pipes = NULL;
static int                  pipe_count = 0;
static volatile sig_atomic_t signal_status = 0;

/* off-screen canvas pre-rendered with the static scene (sky + blue +
   pipes + bricks).  Each animation frame blits this onto stdscr in
   one ncurses call via overwrite(), which clears any prior bubble
   cells, then the bubbles are drawn at their new positions on top. */
static WINDOW              *static_canvas = NULL;

static int  wetpipes_main(void *argument);
static void sighandler(int s);
static void init_colors(void);
static void compute_pipes(void);
static void draw_frame(WINDOW *win);
static void draw_bricks(WINDOW *win, int brick_top);
static void draw_pipe(WINDOW *win, const pipe_t *p);
static void build_static_canvas(void);
static void resize_screen(void);

/* ── vlock hooks ────────────────────────────────────────────────── */

bool vlock_save(void **ctx_ptr)
{
    static struct child_process wetpipes_proc = {
        .function = wetpipes_main,
        .argument = NULL,
        .stdin_fd = REDIRECT_DEV_NULL,
        .stdout_fd = NO_REDIRECT,
        .stderr_fd = NO_REDIRECT,
    };
    GError *tmp_error = NULL;

    initscr();
    savetty();
    nonl();
    cbreak();
    noecho();
    timeout(0);
    leaveok(stdscr, TRUE);
    curs_set(0);
    signal(SIGINT,   sighandler);
    signal(SIGWINCH, sighandler);

    if(!create_child(&wetpipes_proc, &tmp_error))
        return false;

    *ctx_ptr = &wetpipes_proc;
    return true;
}

bool vlock_save_abort(void **ctx_ptr)
{
    struct child_process *proc = *ctx_ptr;

    if(proc != NULL)
    {
        ensure_death(proc->pid);

        curs_set(1);
        clear();
        refresh();
        resetty();
        endwin();

        *ctx_ptr = NULL;
    }

    if(pipes != NULL) { free(pipes); pipes = NULL; }

    return true;
}

/* ── implementation ─────────────────────────────────────────────── */

static void
sighandler(int s)
{
    signal_status = s;
}

static void
init_colors(void)
{
    if(!has_colors()) return;
    start_color();

    /* Brick layer: bricks are brown (dim yellow on most terms), mortar
       glyphs (WACS_BTEE/TTEE) rendered in white = gray on top. */
    init_pair(PAIR_BRICK,       COLOR_WHITE,  COLOR_YELLOW);

    /* Green pipes -- solid main + lighter shade.  index 10 is bright
       green; if the terminal only has 8 colors, init_pair fails and
       the cells will just render in COLOR_GREEN, which still reads
       as green pipes (no highlight). */
    init_pair(PAIR_GREEN_MAIN,  COLOR_GREEN,  COLOR_GREEN);
    init_pair(PAIR_GREEN_LIGHT, COLOR_GREEN + 8, COLOR_GREEN + 8);
    init_pair(PAIR_GREEN_JOIN,  COLOR_GREEN + 8, COLOR_GREEN);

    /* Gray pipes -- bright-black (index 8) is the conventional "gray"
       slot; white (index 7) is the lighter shade. */
    init_pair(PAIR_GRAY_MAIN,   COLOR_BLACK + 8, COLOR_BLACK + 8);
    init_pair(PAIR_GRAY_LIGHT,  COLOR_WHITE,     COLOR_WHITE);
    init_pair(PAIR_GRAY_JOIN,   COLOR_WHITE,     COLOR_BLACK + 8);

    init_pair(PAIR_BG,          COLOR_BLUE,      COLOR_BLUE);
    init_pair(PAIR_BG_SKY,      COLOR_CYAN + 8,  COLOR_CYAN + 8);
    /* bright white (index 15) instead of plain white + A_BOLD -- one
       fewer SGR attribute toggle per cell on terminals that emit a
       separate sequence for the bold bit */
    init_pair(PAIR_BUBBLE,      COLOR_WHITE + 8, COLOR_BLUE);
    /* light blue (index 12 = bright blue) on the blue canvas */
    init_pair(PAIR_BUBBLE_BLUE, COLOR_BLUE + 8,  COLOR_BLUE);
}

/*
    Bubble state transitions.  Every bubble spawns as '.' and may grow
    while sitting on the pipe.  Transitions:

        '.'  -> ':'   (15%)
             -> 'o'   (15%)
             -> stay  (rest)

        'o'  -> '8'   (15%)
             -> 'O'   (15%)
             -> stay  (rest)

        ':', '8', 'O' are terminal.
*/
static bubble_kind_t
bubble_maybe_grow(bubble_kind_t k)
{
    int r;

    switch(k)
    {
        case BUB_DOT:
            r = rand() % 100;
            if(r < 15) return BUB_COLON;
            if(r < 30) return BUB_SMALL;
            return BUB_DOT;
        case BUB_SMALL:
            r = rand() % 100;
            if(r < 15) return BUB_LARGE;
            if(r < 30) return BUB_FAT;
            return BUB_SMALL;
        default:
            return k;       /* ':', '8', 'O' are terminal */
    }
}

/* Every bubble starts as '.' -- they may grow from there while still
   on the pipe (see bubble_maybe_grow). */
static bubble_kind_t
bubble_initial_kind(void)
{
    return BUB_DOT;
}

/* Advance every pipe's bubble pool by one frame: try to emit a new
   bubble if any pipe is due, then move/transition existing bubbles.
   Vanish bubbles that cross above the sky boundary. */
static void
update_bubbles(int frame, int sky_h)
{
    int p, b, c;

    for(p = 0; p < pipe_count; p++)
    {
        pipe_t *pp = &pipes[p];

        /* maybe emit -- pick a random color, then a random stream that
           doesn't already have a bubble of THAT color in flight.  A
           stream can carry up to two bubbles simultaneously (one of
           each color) but never two of the same color. */
        if(frame >= pp->next_emit_frame)
        {
            bubble_color_t color = (rand() & 1) ? BUB_COLOR_WHITE
                                                : BUB_COLOR_BLUE;
            int free_idx[MAX_STREAMS_PER_PIPE];
            int free_count = 0;
            int chosen;

            for(b = 0; b < pp->stream_count; b++)
                if(!pp->bubbles[b][color].alive)
                    free_idx[free_count++] = b;

            if(free_count > 0)
            {
                bubble_t *bb;
                chosen = free_idx[rand() % free_count];
                bb = &pp->bubbles[chosen][color];
                bb->x = pp->stream_x[chosen];
                bb->y = pp->y_top - 1;
                bb->kind = bubble_initial_kind();
                bb->color = color;
                bb->move_counter = 0;
                bb->transition_lockout = TRANSITION_LOCKOUT_FRAMES;
                bb->floating = false;
                bb->alive = true;
            }

            /* schedule the next emit attempt -- essentially every
               frame.  attempts that find every stream busy are
               harmless no-ops; the only effect of a faster cadence
               is that freshly-vanished slots get refilled almost
               immediately. */
            pp->next_emit_frame = frame + (rand() % 2);
        }

        /* update each active bubble (every (stream, color) slot) */
        for(b = 0; b < pp->stream_count; b++)
        for(c = 0; c < BUBBLE_COLOR_COUNT; c++)
        {
            bubble_t *bb = &pp->bubbles[b][c];
            if(!bb->alive) continue;

            if(!bb->floating)
            {
                /* pipe phase: transitions happen here, governed by the
                   lockout.  Once the lockout expires, attempt to grow.
                   If the bubble grew, reset the lockout and stay on
                   the pipe.  If it didn't grow (terminal kind or random
                   roll declined), release the bubble into the floating
                   phase -- from then on, the kind is locked. */
                if(bb->transition_lockout > 0)
                {
                    bb->transition_lockout--;
                }
                else
                {
                    bubble_kind_t old_kind = bb->kind;
                    bubble_kind_t new_kind = bubble_maybe_grow(old_kind);
                    if(new_kind != old_kind)
                    {
                        bb->kind = new_kind;
                        bb->transition_lockout = TRANSITION_LOCKOUT_FRAMES;
                    }
                    else
                    {
                        bb->floating = true;
                        bb->move_counter = 0;
                    }
                }
            }
            else
            {
                /* floating phase: kind is locked; just rise at the
                   period for its kind. */
                bb->move_counter++;
                if(bb->move_counter >= bubble_period[bb->kind])
                {
                    bb->y--;
                    bb->move_counter = 0;

                    /* vanish at the surface (sky boundary) */
                    if(bb->y < sky_h) bb->alive = false;
                }
            }
        }
    }
}

/*
    Draw every alive bubble onto stdscr at its current position.
    Called each frame AFTER overwrite(static_canvas, stdscr) has
    blitted the static scene -- the blit naturally cleared every
    previous-frame bubble cell, so we don't need a separate erase
    pass at all.  ncurses' refresh diffs the final stdscr against the
    physical terminal and pushes only the cells that genuinely
    changed (in practice: old bubble cells now showing bg, new
    bubble cells now showing the glyph).
*/
static void
draw_bubbles(void)
{
    int p, b, c;
    int current_pair = -1;

    /* group writes by color to minimize SGR toggles -- white bubbles
       first, then blue bubbles */
    for(c = 0; c < BUBBLE_COLOR_COUNT; c++)
    {
        int pair = (c == BUB_COLOR_WHITE) ? PAIR_BUBBLE : PAIR_BUBBLE_BLUE;
        if(pair != current_pair)
        {
            if(current_pair >= 0) attroff(COLOR_PAIR(current_pair));
            attron(COLOR_PAIR(pair));
            current_pair = pair;
        }

        for(p = 0; p < pipe_count; p++)
        {
            for(b = 0; b < pipes[p].stream_count; b++)
            {
                bubble_t *bb = &pipes[p].bubbles[b][c];
                if(!bb->alive) continue;
                mvaddch(bb->y, bb->x, bubble_glyph[bb->kind]);
            }
        }
    }
    if(current_pair >= 0) attroff(COLOR_PAIR(current_pair));
}

/*
    Lay out 2-3 pipes across the bottom of the screen.  All pipes share
    the same shaft width (DEFAULT_SHAFT_W when there's room, shrunk
    proportionally on narrow terminals).  Each pipe gets a random kind
    (green/gray) and a random height up to 30% of LINES.  Pipes are
    distributed by dividing the screen into equal-width slots and
    centering one pipe in each.
*/
static void
compute_pipes(void)
{
    int     brick_top    = LINES - BRICK_ROWS;
    int     max_height   = (LINES * PIPE_HEIGHT_FRACTION) / 100;
    int     shaft_w      = DEFAULT_SHAFT_W;
    int     flange_w;
    int     count;
    int     slot_w;
    int     i;

    if(max_height < MIN_PIPE_HEIGHT) max_height = MIN_PIPE_HEIGHT;

    /* Pick 2 or 3 pipes, then drop down to 2 if 3 won't fit even with
       a minimal gap.  Shaft width must stay even (the underline split
       relies on shaft_w / 2). */
    count = MIN_PIPES + (rand() % (MAX_PIPES - MIN_PIPES + 1));

    while(count > 0)
    {
        int needed = count * (shaft_w + 2) + (count - 1) * MIN_PIPE_GAP;
        if(needed <= COLS) break;
        /* try a narrower (but still even) shaft width */
        if(shaft_w > 6) shaft_w -= 2;
        else count--;
    }
    if(count <= 0) { pipe_count = 0; return; }

    flange_w = shaft_w + 2;
    slot_w   = COLS / count;

    if(pipes != NULL) { free(pipes); pipes = NULL; }
    pipes = calloc(count, sizeof(pipe_t));
    if(pipes == NULL) { pipe_count = 0; return; }

    for(i = 0; i < count; i++)
    {
        int height = MIN_PIPE_HEIGHT
            + (rand() % (max_height - MIN_PIPE_HEIGHT + 1));
        int slot_x = i * slot_w;
        int pipe_x = slot_x + (slot_w - flange_w) / 2;
        int sc, s;

        if(height > max_height) height = max_height;

        pipes[i].x       = pipe_x;
        pipes[i].y_top   = brick_top - height;
        pipes[i].shaft_w = shaft_w;
        pipes[i].height  = height;
        pipes[i].kind    = (rand() & 1) ? PIPE_KIND_GREEN : PIPE_KIND_GRAY;

        /* Fixed stream count per pipe -- the shaft is sized to give
           exactly shaft_w - 1 = 12 usable columns. */
        sc = MAX_STREAMS_PER_PIPE;
        if(sc > shaft_w - 1) sc = shaft_w - 1;
        if(sc < 1) sc = 1;
        pipes[i].stream_count = sc;
        for(s = 0; s < sc; s++)
        {
            /* spread across the shaft, leaving 1 col on each side */
            int span = shaft_w - 2;
            int off  = (sc == 1) ? span / 2
                                 : (s * span) / (sc - 1);
            pipes[i].stream_x[s] = pipe_x + 1 + 1 + off;
        }
        pipes[i].next_emit_frame = rand() % 20;
        /* calloc zeroed the bubbles[] array, .alive=false already */
    }

    /* Make sure the pipe colors aren't all the same.  With 2-3 pipes
       and uniform random picks there's a 1/4 to 1/8 chance every
       pipe ends up the same color -- if that happened, flip one at
       random so the user always sees a mix. */
    if(count >= 2)
    {
        bool has_green = false;
        bool has_gray  = false;
        for(i = 0; i < count; i++)
        {
            if(pipes[i].kind == PIPE_KIND_GREEN) has_green = true;
            else                                 has_gray  = true;
        }
        if(!has_green || !has_gray)
        {
            int flip = rand() % count;
            pipes[flip].kind = has_green ? PIPE_KIND_GRAY
                                         : PIPE_KIND_GREEN;
        }
    }

    pipe_count = count;
}

static void
draw_bricks(WINDOW *win, int brick_top)
{
    /* Small Bricks tile, identical to vwm's wallpaper rendering:
       BTEE at (y%2)==(x%2), TTEE otherwise.  Use the legacy ACS_*
       chtype constants so the module links against plain ncurses
       (no ncursesw dependency for WACS_*). */
    int colors = COLOR_PAIR(PAIR_BRICK) | A_ALTCHARSET;
    int y, x;

    wattron(win, colors);
    for(y = 0; y < BRICK_ROWS; y++)
    {
        for(x = 0; x < COLS; x++)
        {
            chtype ch = ((y & 1) == (x & 1)) ? ACS_BTEE : ACS_TTEE;
            mvwaddch(win, brick_top + y, x, ch);
        }
    }
    wattroff(win, colors);
}

/*
    Draw one Mario-style pipe.  Geometry (F = shaft_w + 2):

        y_top      L M M M M M M M M  <- flange top: leftmost col is
                                          lighter (the left-edge
                                          highlight stripe runs all the
                                          way up to here), rest is main
        y_top+1    L _ _ _ M M M M M  <- flange bottom + separator: same
                                          left-edge highlight; the
                                          underline char ('_') in
                                          lighter on main for the left
                                          half of the shaft area; main
                                          solid for the right half and
                                          the right overhang
        y_top+2      L M M M M M M    <- shaft body: lighter at the
        y_top+3      L M M M M M M       shaft's leftmost column,
        ...                              solid main for the rest

    Pipe occupies pipe_x..pipe_x+F-1 in flange rows; pipe_x+1..pipe_x+S
    in shaft rows (F is 2 wider than S to account for the overhang).
*/
static void
draw_pipe(WINDOW *win, const pipe_t *p)
{
    int     main_pair, light_pair, join_pair;
    int     F        = p->shaft_w + 2;
    int     shaft_x  = p->x + 1;
    int     body_y   = p->y_top + FLANGE_ROWS;
    int     body_h   = p->height - FLANGE_ROWS;
    int     half     = p->shaft_w / 2;
    int     join_y;
    int     row;

    if(p->kind == PIPE_KIND_GREEN)
    {
        main_pair  = PAIR_GREEN_MAIN;
        light_pair = PAIR_GREEN_LIGHT;
        join_pair  = PAIR_GREEN_JOIN;
    }
    else
    {
        main_pair  = PAIR_GRAY_MAIN;
        light_pair = PAIR_GRAY_LIGHT;
        join_pair  = PAIR_GRAY_JOIN;
    }

    /* flange top row: leftmost col is the highlight stripe, rest is
       solid main (no underline yet -- that's on the bottom row) */
    wattron(win, COLOR_PAIR(light_pair));
    mvwaddch(win, p->y_top, p->x, ' ');
    wattroff(win, COLOR_PAIR(light_pair));

    wattron(win, COLOR_PAIR(main_pair));
    mvwhline(win, p->y_top, p->x + 1, ' ', F - 1);
    wattroff(win, COLOR_PAIR(main_pair));

    /* flange bottom row doubles as the join/separator: highlight at
       col 0, then underlines in lighter on main bg all the way across
       the rest of the row (shaft area + right overhang). */
    join_y = p->y_top + FLANGE_ROWS - 1;
    (void)half;

    wattron(win, COLOR_PAIR(light_pair));
    mvwaddch(win, join_y, p->x, ' ');
    wattroff(win, COLOR_PAIR(light_pair));

    wattron(win, COLOR_PAIR(join_pair));
    mvwhline(win, join_y, p->x + 1, '_', F - 1);
    wattroff(win, COLOR_PAIR(join_pair));

    /* shaft body: highlight at the shaft's leftmost column (one cell
       to the right of the flange's left overhang), solid main for the
       remaining shaft cells */
    for(row = 0; row < body_h; row++)
    {
        wattron(win, COLOR_PAIR(light_pair));
        mvwaddch(win, body_y + row, shaft_x, ' ');
        wattroff(win, COLOR_PAIR(light_pair));

        wattron(win, COLOR_PAIR(main_pair));
        mvwhline(win, body_y + row, shaft_x + 1, ' ', p->shaft_w - 1);
        wattroff(win, COLOR_PAIR(main_pair));
    }
}

static void
draw_frame(WINDOW *win)
{
    int brick_top = LINES - BRICK_ROWS;
    int sky_h    = (LINES * SKY_FRACTION) / 100;
    int i;

    if(sky_h < 1)         sky_h = 1;
    if(sky_h > brick_top) sky_h = brick_top;

    /* top strip: bright blue (sky / water) */
    wattron(win, COLOR_PAIR(PAIR_BG_SKY));
    for(i = 0; i < sky_h; i++)
        mvwhline(win, i, 0, ' ', COLS);
    wattroff(win, COLOR_PAIR(PAIR_BG_SKY));

    /* mid section: solid blue, above the bricks */
    wattron(win, COLOR_PAIR(PAIR_BG));
    for(i = sky_h; i < brick_top; i++)
        mvwhline(win, i, 0, ' ', COLS);
    wattroff(win, COLOR_PAIR(PAIR_BG));

    /* pipes first, then bricks: the bricks will overwrite any pipe
       cells that extend into the brick zone (shouldn't happen, but
       safe) and provide the visible "ground" under the pipes. */
    for(i = 0; i < pipe_count; i++)
        draw_pipe(win, &pipes[i]);

    draw_bricks(win, brick_top);
}

/*
    (Re)create static_canvas at current LINES x COLS and render the
    entire static scene to it.  Called at startup and again after a
    SIGWINCH resize so subsequent overwrite() blits give the right
    geometry.
*/
static void
build_static_canvas(void)
{
    if(static_canvas != NULL)
    {
        delwin(static_canvas);
        static_canvas = NULL;
    }
    static_canvas = newwin(LINES, COLS, 0, 0);
    if(static_canvas == NULL) return;
    draw_frame(static_canvas);
}

static void
resize_screen(void)
{
    char            *tty;
    int              fd;
    struct winsize   win;

    tty = ttyname(STDOUT_FILENO);
    if(tty == NULL) return;

    fd = open(tty, O_RDONLY);
    if(fd == -1) return;

    if(ioctl(fd, TIOCGWINSZ, &win) == -1)
    {
        close(fd);
        return;
    }
    close(fd);

    if(win.ws_col < 10) win.ws_col = 10;
    if(win.ws_row < 10) win.ws_row = 10;

    COLS  = win.ws_col;
    LINES = win.ws_row;

    resizeterm(LINES, COLS);
    wresize(stdscr, LINES, COLS);

    clear();
    compute_pipes();
}

static int
wetpipes_main(void *argument)
{
    (void)argument;

    srand((unsigned)time(NULL));

    if(LINES < 10) LINES = 10;
    if(COLS  < 10) COLS  = 10;

    /* the child inherits parent's ncurses state across fork; make sure
       non-blocking input is on for our wgetch poll, and wipe the canvas
       so stale parent-rendered content doesn't peek through */
    nodelay(stdscr, TRUE);
    clear();
    refresh();

    init_colors();

    /* set stdscr's background to a blue space so ncurses' clr_eol
       optimization (CSI K) clears to blue rather than the terminal's
       default black -- otherwise updates to a bubble row briefly
       paint the rest of the line black */
    bkgdset(' ' | COLOR_PAIR(PAIR_BG));

    compute_pipes();
    build_static_canvas();
    overwrite(static_canvas, stdscr);
    info_box_draw();
    refresh();

    {
        int frame = 0;
        int sky_h = (LINES * SKY_FRACTION) / 100;
        if(sky_h < 1) sky_h = 1;

        while(1)
        {
            if(signal_status == SIGINT) return 0;
            if(signal_status == SIGWINCH)
            {
                resize_screen();
                sky_h = (LINES * SKY_FRACTION) / 100;
                if(sky_h < 1) sky_h = 1;
                /* rebuild canvas at the new geometry */
                build_static_canvas();
                signal_status = 0;
            }

            /* drain input non-blockingly -- the parent vlock-main is
               the one watching for the wake key and will kill us via
               vlock_save_abort.  Just discard anything we see. */
            (void)wgetch(stdscr);

            update_bubbles(frame, sky_h);

            /* blit the pre-rendered static scene onto stdscr (this
               clears every prior bubble cell), overlay the bubbles at
               their new positions, and push */
            overwrite(static_canvas, stdscr);
            draw_bubbles();
            info_box_draw();
            wnoutrefresh(stdscr);
            doupdate();

            napms(80);
            frame++;
        }
    }
}
