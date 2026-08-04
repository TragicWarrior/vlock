/* netware.c -- a screen saving plugin for vlock: CPU util-driven "worms"
 *
 * One worm per physical CPU from sysconf(_SC_NPROCESSORS_ONLN).
 * Length tracks load (global util from /proc/stat as a simple proxy).
 * Worms random-walk; denser body near the head, lighter toward the tail.
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
#include <locale.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <ncurses.h>

#include "process.h"
#include "vlock_plugin.h"
#include "info_box.h"

/* ------------------------------------------------------------------ */
/* no depends[]: see wetpipes.c for the rationale.                     */
/* ------------------------------------------------------------------ */

#define WORM_MAX_LEN           48
#define WORM_MIN_LEN            4
#define MAX_CPUS               32

/* Move every MOVE_EVERY frames; with napms(FRAME_MS) this is ~4 steps/sec. */
#define FRAME_MS               80
#define MOVE_EVERY              3

#define COLOR_MIN               1
/* pairs 1-7 and 9 (skip 8 = info_box) */
#define COLOR_MAX               9

/* 8 directions: N, NE, E, SE, S, SW, W, NW (y grows downward). */
static const int DX8[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static const int DY8[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

typedef struct {
    int x[WORM_MAX_LEN];
    int y[WORM_MAX_LEN];
    int x_prev[WORM_MAX_LEN];
    int y_prev[WORM_MAX_LEN];
    int length;
    int length_prev;
    int dir;
    int runlength;
    int tick;               /* frames until next move */
    short pair;
} worm_t;

static worm_t               *worms = NULL;
static int                   ncpus = 0;
static int                   ascii_mode = 0;
static int                   frame = 0;
static volatile sig_atomic_t signal_status = 0;

static int  netware_main(void *arg);
static void sighandler(int s);
static void read_ascii_mode(void);
static void init_colors(void);
static float get_cpu_utilization(void);
static void init_worm(worm_t *w, int cpu);
static void update_worm(worm_t *w, float cpu_util);
static void erase_worm_prev(const worm_t *w);
static void draw_worm(const worm_t *w);
static void resize_screen(void);

/* ------------------------------------------------------------------ */

static void
sighandler(int s)
{
    signal_status = s;
}

static void
read_ascii_mode(void)
{
    const char *ev = getenv("VLOCK_ASCII");
    char c;

    if (ev == NULL || *ev == '\0')
        return;

    c = *ev;
    if (c >= 'A' && c <= 'Z')
        c = (char)(c + 32);

    if (c == '1' || c == 'y' || c == 't' || c == 'o')
        ascii_mode = 1;
    else if (strncmp(ev, "yes", 3) == 0)
        ascii_mode = 1;
}

static void
init_colors(void)
{
    static const short fg[] = {
        COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_BLUE,
        COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE, COLOR_WHITE,
    };
    int p, fi;

    if (!has_colors())
        return;

    start_color();

    /* pairs 1..7 */
    for (p = 1; p <= 7; p++)
        init_pair((short)p, fg[p - 1], COLOR_BLACK);

    /* pair 9 (skip 8) */
    init_pair(9, COLOR_WHITE, COLOR_BLACK);
    (void)fi;
}

static float
get_cpu_utilization(void)
{
    static long prev_act = -1, prev_all = -1;
    FILE *fp;
    char buf[256];
    long user, nice, sys, idle, iowait, irq, softirq, steal;
    long act, all, d_act, d_all;
    float util;

    fp = fopen("/proc/stat", "r");
    if (fp == NULL)
        return 0.2f;

    /* Aggregate "cpu " line only. */
    if (fgets(buf, sizeof buf, fp) == NULL) {
        fclose(fp);
        return 0.2f;
    }
    fclose(fp);

    user = nice = sys = idle = iowait = irq = softirq = steal = 0;
    if (sscanf(buf, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) < 4)
        return 0.2f;

    act = user + nice + sys + irq + softirq + steal;
    all = act + idle + iowait;

    if (prev_all < 0) {
        prev_act = act;
        prev_all = all;
        return 0.2f;
    }

    d_act = act - prev_act;
    d_all = all - prev_all;
    prev_act = act;
    prev_all = all;

    if (d_all <= 0)
        return 0.2f;

    util = (float)d_act / (float)d_all;
    if (util < 0.05f)
        util = 0.05f;
    if (util > 0.95f)
        util = 0.95f;
    return util;
}

static void
init_worm(worm_t *w, int cpu)
{
    int i, hx, hy, dir;

    memset(w, 0, sizeof *w);

    hx = (COLS > 1) ? rand() % COLS : 0;
    hy = (LINES > 1) ? rand() % LINES : 0;
    dir = rand() % 8;

    w->dir = dir;
    w->length = WORM_MIN_LEN;
    w->length_prev = 0;
    w->runlength = WORM_MIN_LEN;
    w->tick = cpu % MOVE_EVERY;  /* stagger starts */
    /* pairs 1-7,9 */
    w->pair = (short)(1 + (cpu % 7));
    if (w->pair >= 8)
        w->pair = 9;

    for (i = 0; i < WORM_MAX_LEN; i++) {
        int x = hx - DX8[dir] * i;
        int y = hy - DY8[dir] * i;

        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x >= COLS)
            x = COLS - 1;
        if (y >= LINES)
            y = LINES - 1;

        w->x[i] = x;
        w->y[i] = y;
        w->x_prev[i] = x;
        w->y_prev[i] = y;
    }
}

static void
update_worm(worm_t *w, float cpu_util)
{
    int desired, n, dir, x, y, rnd;

    desired = WORM_MIN_LEN + (int)(cpu_util * (WORM_MAX_LEN - WORM_MIN_LEN));
    if (desired < WORM_MIN_LEN)
        desired = WORM_MIN_LEN;
    if (desired >= WORM_MAX_LEN)
        desired = WORM_MAX_LEN - 1;

    /* Grow/shrink by at most 1 segment per step for smooth trails. */
    if (desired > w->length)
        w->length++;
    else if (desired < w->length && w->length > WORM_MIN_LEN)
        w->length--;

    dir = w->dir;
    x = w->x[0];
    y = w->y[0];

    /* Occasional turns. */
    if (w->runlength <= 0) {
        rnd = rand() % 128;
        if (rnd > 90)
            dir = (dir + 2) % 8;
        else if (rnd == 1)
            dir = (dir + 1) % 8;
        else if (rnd == 2)
            dir = (dir + 7) % 8;
        w->runlength = w->length;
    } else {
        w->runlength--;
        rnd = rand() % 128;
        if (rnd == 1)
            dir = (dir + 1) % 8;
        else if (rnd == 2)
            dir = (dir + 7) % 8;
    }

    x += DX8[dir];
    y += DY8[dir];

    /* Bounce at edges. */
    if (x < 0) {
        x = 0;
        dir = (dir + 4) % 8;
    } else if (x >= COLS) {
        x = COLS - 1;
        dir = (dir + 4) % 8;
    }
    if (y < 0) {
        y = 0;
        dir = (dir + 4) % 8;
    } else if (y >= LINES) {
        y = LINES - 1;
        dir = (dir + 4) % 8;
    }

    w->dir = dir;

    for (n = w->length - 1; n > 0; n--) {
        w->x[n] = w->x[n - 1];
        w->y[n] = w->y[n - 1];
    }
    w->x[0] = x;
    w->y[0] = y;
}

/* Erase cells drawn last frame (selective — no full clear). */
static void
erase_worm_prev(const worm_t *w)
{
    int n;

    attrset(A_NORMAL);
    for (n = 0; n < w->length_prev; n++) {
        int yy = w->y_prev[n];
        int xx = w->x_prev[n];

        if (yy >= 0 && yy < LINES && xx >= 0 && xx < COLS)
            mvaddch(yy, xx, ' ');
    }
}

static void
draw_worm(const worm_t *w)
{
    int n, div, mod, c;
    short pair = w->pair;
    attr_t attr;
    chtype ch;

    if (pair == 8)
        pair = 1;

    div = w->length / 4;
    mod = w->length % 4;
    if (div <= 0)
        div = 1;

    for (n = w->length - 1; n >= 0; n--) {
        int is_head = (n == 0);

        if (is_head) {
            attr = A_BOLD | A_REVERSE;
            if (has_colors())
                attr |= COLOR_PAIR(pair);
            if (ascii_mode)
                mvaddch(w->y[n], w->x[n], (chtype)'#' | COLOR_PAIR(pair));
            else
                mvaddch(w->y[n], w->x[n], ' ' | attr);
            continue;
        }

        c = n < (div + 1) * mod ? n / (div + 1) : (n - mod) / div;
        c %= 4;

        if (c >= 3)
            attr = A_DIM;
        else
            attr = A_NORMAL;
        if (has_colors())
            attr |= COLOR_PAIR(pair);

        if (ascii_mode) {
            static const char sh[4] = { '#', '%', '+', ':' };
            mvaddch(w->y[n], w->x[n], (chtype)sh[c] | attr);
        } else {
            if (c == 0)
                ch = ACS_CKBOARD | A_BOLD;
            else if (c == 1)
                ch = ACS_CKBOARD;
            else if (c == 2)
                ch = ACS_CKBOARD | A_DIM;
            else
                ch = ACS_BULLET | A_DIM;
            mvaddch(w->y[n], w->x[n], ch | (has_colors() ? COLOR_PAIR(pair) : 0));
        }
    }
}

static void
resize_screen(void)
{
    char *tty;
    int fd;
    struct winsize win;
    int i, n;

    tty = ttyname(STDOUT_FILENO);
    if (tty == NULL)
        return;

    fd = open(tty, O_RDONLY);
    if (fd < 0)
        return;

    if (ioctl(fd, TIOCGWINSZ, &win) == 0) {
        if (win.ws_col >= 10)
            COLS = win.ws_col;
        if (win.ws_row >= 10)
            LINES = win.ws_row;
        resizeterm(LINES, COLS);
        wresize(stdscr, LINES, COLS);
        clear();
        refresh();

        for (i = 0; i < ncpus; i++) {
            worm_t *w = &worms[i];

            for (n = 0; n < w->length; n++) {
                if (w->x[n] >= COLS)
                    w->x[n] = COLS - 1;
                if (w->y[n] >= LINES)
                    w->y[n] = LINES - 1;
                if (w->x[n] < 0)
                    w->x[n] = 0;
                if (w->y[n] < 0)
                    w->y[n] = 0;
            }
            w->length_prev = 0;
        }
    }
    close(fd);
}

bool vlock_save(void **ctx_ptr)
{
    static struct child_process netware_proc = {
        .function = netware_main,
        .argument = NULL,
        .stdin_fd  = REDIRECT_DEV_NULL,
        .stdout_fd = NO_REDIRECT,
        .stderr_fd = NO_REDIRECT,
    };

    setlocale(LC_ALL, "");

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

    if (!create_child(&netware_proc, NULL))
        return false;

    *ctx_ptr = &netware_proc;
    return true;
}

bool vlock_save_abort(void **ctx_ptr)
{
    struct child_process *proc = *ctx_ptr;

    if (proc != NULL && proc->pid > 0)
        ensure_death(proc->pid);

    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();

    if (worms != NULL) {
        free(worms);
        worms = NULL;
    }
    ncpus = 0;
    *ctx_ptr = NULL;
    return true;
}

static int
netware_main(void *arg)
{
    int i, n;

    (void)arg;

    ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus <= 0)
        ncpus = 1;
    if (ncpus > MAX_CPUS)
        ncpus = MAX_CPUS;

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    read_ascii_mode();

    nodelay(stdscr, TRUE);
    clear();
    refresh();
    init_colors();
    bkgdset(' ' | A_NORMAL);

    worms = calloc((size_t)ncpus, sizeof(worm_t));
    if (worms == NULL)
        return 1;

    for (i = 0; i < ncpus; i++)
        init_worm(&worms[i], i);

    while (1) {
        float util;

        if (signal_status == SIGINT)
            break;

        if (signal_status == SIGWINCH) {
            resize_screen();
            signal_status = 0;
        }

        (void)wgetch(stdscr);

        util = get_cpu_utilization();

        /* Erase previous frame's worm cells (no full-screen clear). */
        for (i = 0; i < ncpus; i++)
            erase_worm_prev(&worms[i]);

        /* Throttled motion: only move every MOVE_EVERY frames. */
        if ((frame % MOVE_EVERY) == 0) {
            for (i = 0; i < ncpus; i++) {
                worm_t *w = &worms[i];

                /* Snapshot current geometry as "prev" after erase used old prev. */
                for (n = 0; n < w->length; n++) {
                    w->x_prev[n] = w->x[n];
                    w->y_prev[n] = w->y[n];
                }
                w->length_prev = w->length;

                update_worm(w, util);
            }
        } else {
            /* Still refresh prev = current so erase stays correct next frame. */
            for (i = 0; i < ncpus; i++) {
                worm_t *w = &worms[i];

                for (n = 0; n < w->length; n++) {
                    w->x_prev[n] = w->x[n];
                    w->y_prev[n] = w->y[n];
                }
                w->length_prev = w->length;
            }
        }

        for (i = 0; i < ncpus; i++)
            draw_worm(&worms[i]);

        info_box_draw();
        wnoutrefresh(stdscr);
        doupdate();

        napms(FRAME_MS);
        frame++;
    }

    free(worms);
    worms = NULL;
    ncpus = 0;
    return 0;
}
