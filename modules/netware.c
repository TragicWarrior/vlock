/* netware.c -- a screen saving plugin for vlock: CPU util-driven "worms"
 *
 * One worm per physical CPU from sysconf(_SC_NPROCESSORS_ONLN).  On each
 * animation frame the worms read /proc/stat to estimate overall CPU
 * utilisation; that value drives each worm's segment length (more load =
 * longer segments, up to a cap).  Worms wander via random walk in eight
 * directions and are drawn with '#' or ACS_CKBOARD characters.  Colours
 * rotate across pseudo-random ncurses pairs chosen from every pair except
 * 8 (the info_box overlay occupies that slot).
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

#define WORM_MAX_LEN           200
#define WORM_INIT_LEN          8

#define COLOR_MIN              1
#define COLOR_MAX              (COLOR_MIN + 7)   /* pairs 1-7; pair 9 after skip */

static const int DX8[8] = { -1,  0,  1,  0, -1,  1,  1, -1 };
static const int DY8[8] = { -1, -1, -1,  1,  0,  0,  1, -1 };

/* ------------------------------------------------------------------ */
typedef struct {
    int x, y;                       /* head position */
    int dir;                        /* current direction index (0..7) */
    short cur_pair;                 /* colour pair used for this worm */
    int tail_x[WORM_MAX_LEN];       /* ring buffer of segment coords */
    int tail_y[WORM_MAX_LEN];
    int head_idx;                   /* next tail slot to write */
    int live_len;                   /* current chain length */
} worm_t;

static worm_t *worms      = NULL;
static int     ncpus      = 0;
static short   next_color = COLOR_MIN;
static int     ascii_mode = 0;                    /* plain ASCII body char    */
static volatile sig_atomic_t signal_status = 0;

/* ------------------------------------------------------------------ */
/* ascii_mode: 0 = use ACS_CKBOARD dim body; 1 = plain '#' (ASCII).  */
/* Read once from VLOCK_ASCII at startup.                             */
/* True-y values: 1, y, yes, true, on (case-insensitive).            */
/* ------------------------------------------------------------------ */
static void read_ascii_mode(void)
{
    const char *ev = getenv("VLOCK_ASCII");
    if (!ev || !*ev) return;

    /* Convert first character to lowercase for comparison. */
    char c = *ev;
    if (c >= 'A' && c <= 'Z') c += 32;

    if (c == '1' || c == 'y' || c == 't' || c == 'o') {
        ascii_mode = 1;
    } else if (strncmp(ev, "yes", 3) == 0) {
        ascii_mode = 1;
    }
}

/* ------------------------------------------------------------------ */
/* advance_color -- round-robin across pairs, skip pair 8 (info_box).  */
/* ------------------------------------------------------------------ */

static void advance_color(void)
{
    do {
        next_color++;
        if (next_color > COLOR_MAX + 1)   /* pair 9 is the last after skip */
            next_color = COLOR_MIN;
    } while (next_color == 8);
}

/* ------------------------------------------------------------------ */
static void init_colors(void)
{
    if (!has_colors()) return;
    start_color();

    static const short fg[] = {
        COLOR_RED,
        COLOR_GREEN,
        COLOR_YELLOW,
        COLOR_BLUE,
        COLOR_MAGENTA,
        COLOR_CYAN,
        COLOR_WHITE + 8,   /* bright white -- pair 7 */
        COLOR_WHITE,       /* pair 9 (after skip) */
    };

    for (int p = COLOR_MIN; p <= COLOR_MAX; p++) {
        int fi = p - COLOR_MIN;
        init_pair((short)p, fg[fi], COLOR_BLACK);
    }
    next_color = COLOR_MIN;
}

/* ------------------------------------------------------------------ */
/* get_cpu_utilization -- read /proc/stat and return (user+nice+sys)/total. */
/* Only the first 4 fields (user, nice, system, idle) per CPU line     */
/* are needed; we skip cpu0 aggregate and average across all CPUs.      */
/* ------------------------------------------------------------------ */

static float get_cpu_utilization(void)
{
    FILE *fp = fopen("/proc/stat", "r");
    char   buf[2048];
    long   total_act = 0, total_all = 0;
    int    nparsed   = 0;

    if (!fp) return 0.3f;

    while (fgets(buf, sizeof buf, fp)) {
        int idx;
        long a[4];
        /* "cpuN user nice sys idle ..." */
        if (sscanf(buf, "cpu%d %ld %ld %ld %ld", &idx,
                   &a[0], &a[1], &a[2], &a[3]) < 4)
            continue;
        /* Skip the first aggregate "cpu0" line. */
        total_act   += a[0] + a[1] + a[2];
        total_all   += a[0] + a[1] + a[2] + a[3];
        nparsed++;
    }
    fclose(fp);

    if (total_all <= 0 || nparsed == 0) return 0.3f;

    float util = (float)total_act / (float)total_all;
    if (util < 0.01f) util = 0.01f;
    if (util > 0.95f) util = 0.95f;
    return util;
}

/* ------------------------------------------------------------------ */
static void new_worm(void)
{
    if (ncpus == 0) return;

    int idx       = rand() % ncpus;
    worm_t *w     = &worms[idx];

    /* Random position. */
    w->x        = rand() % COLS;
    w->y        = rand() % LINES;
    w->dir      = rand() % 8;
    w->cur_pair = next_color;
    advance_color();

    /* Length scaled by CPU utilisation. */
    float ut   = get_cpu_utilization();
    int lo       = WORM_INIT_LEN + (int)(ut * WORM_MAX_LEN * 0.4f);
    int hi       = WORM_INIT_LEN + (int)(ut * WORM_MAX_LEN * 0.85f);
    if (hi < lo)  hi = lo;
    w->live_len  = lo + rand() % (hi - lo + 1);

    /* Seed the tail ring buffer: walk backwards from head for initial
       trace so the worm does not start as a giant dot. */
    int sx     = w->x, sy   = w->y;
    static int turns[] = { 0, 1, -1, 2 };
    for (int i = 0; i < WORM_INIT_LEN - 1 && i < WORM_MAX_LEN; i++) {
        /* approximate reverse step */
        sx -= DX8[w->dir] * turns[rand() % 4];
        sy -= DY8[w->dir] * turns[rand() % 4];
    }

    w->head_idx = 0;
    for (int i = 0; i < WORM_INIT_LEN && i < WORM_MAX_LEN; i++) {
        w->tail_x[i] = sx;
        w->tail_y[i] = sy;
    }
}

/* ------------------------------------------------------------------ */
static void update_worm(worm_t *w, float cpu_util)
{
    /* Desired tail length grows with CPU usage. */
    int desired  = WORM_INIT_LEN + (int)(cpu_util * WORM_MAX_LEN);
    if (desired < WORM_INIT_LEN)     desired = WORM_INIT_LEN;
    if (desired >= WORM_MAX_LEN)     desired = WORM_MAX_LEN - 1;
    w->live_len  = desired;

    /* Pick direction: bias 75% toward current, ±1 turn. */
    int d;
    if ((rand() & 3) == 0)       /* 25 % randomize            */
        d = rand() % 8;
    else {                        /* 75 % stay / slight turn  */
        int delta = (rand() & 3) - 1;   /* -1, 0, +1           */
        d = (w->dir + delta);
        if (d < 0)      d = 7;
        else if (d > 7)  d = 0;
    }

    w->x += DX8[d];
    w->y += DY8[d];

    /* Wrap at screen edges. */
    if (w->x < 0)      w->x = COLS - 1;
    if (w->x >= COLS)  w->x = 0;
    if (w->y < 0)      w->y = LINES - 1;
    if (w->y >= LINES) w->y = 0;

    /* Push new head into tail ring buffer. */
    int slot     = w->head_idx % WORM_MAX_LEN;
    w->tail_x[slot] = w->x;
    w->tail_y[slot] = w->y;
    w->head_idx++;
}

/* ------------------------------------------------------------------ */
/* Draw all worm segments onto stdscr.  Called each frame after update.*/
/* No erase needed -- overwrite pass of previous content via the tail   */
/* ring buffer (each cell is painted every frame).                       */
/* ------------------------------------------------------------------ */

static void draw_frame(void)
{
    for (int i = 0; i < ncpus && worms[i].live_len > 1; i++) {
        worm_t *w      = &worms[i];
        short   pair   = w->cur_pair;
        int     len    = w->live_len;
        if (len > WORM_MAX_LEN)              len = WORM_MAX_LEN;

        /* Compute start slot in ring buffer. */
        int start  = (w->head_idx - len) % WORM_MAX_LEN;
        if (start < 0)     start += WORM_MAX_LEN;

        /* Skip pair 8: substitute nearby pair. */
        if (pair == 8)         pair = COLOR_MIN;   /* fallback */

        attron(COLOR_PAIR(pair));

        /* Draw tail: alternating dim ACS_CKBOARD ('lightest') and solid '#' (body).
           In ascii_mode the body is also plain '#' (no multi-byte glyphs).     */
        for (int j = 1; j < len; j++) {
            int s     = (start + j) % WORM_MAX_LEN;
            if ((j & 1) == 0) {
                attron(COLOR_PAIR(pair) | A_DIM);
                mvaddch(w->tail_y[s], w->tail_x[s], ascii_mode ? '#' : ACS_CKBOARD);
                attroff(COLOR_PAIR(pair) | A_DIM);
            } else {
                char ch = '#';
                attron(COLOR_PAIR(pair));
                mvaddch(w->tail_y[s], w->tail_x[s], ch);
                attroff(COLOR_PAIR(pair));
            }
        }

        /* Head -- solid bright block (reverse-video space) in the worm's colour. */
        attron(COLOR_PAIR(pair) | A_REVERSE | A_BOLD);
        mvaddch(w->y, w->x, ' ');
        attroff(COLOR_PAIR(pair) | A_REVERSE | A_BOLD);

        attroff(COLOR_PAIR(pair));
    }
}

/* ─── vlock hooks (the dlopen-able entry points) ──────────────── */

static int netware_main(void *arg);
static void sighandler(int s)
{
    signal_status = s;
}

static void cleanup_worms(void)
{
    if (worms != NULL) {
        free(worms);
        worms      = NULL;
        ncpus      = 0;
        next_color = COLOR_MIN;
    }
}

bool vlock_save(void **ctx_ptr)
{
    static struct child_process netware_proc = {
        .function     = netware_main,
        .argument     = NULL,
        .stdin_fd     = REDIRECT_DEV_NULL,
        .stdout_fd    = NO_REDIRECT,
        .stderr_fd    = NO_REDIRECT,
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
    struct child_process *proc   = *ctx_ptr;

    if (proc != NULL && proc->pid > 0)
        ensure_death(proc->pid);

    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();

    cleanup_worms();

    *ctx_ptr = NULL;
    return true;
}

/* ─── main worker loop (runs in child process) ────────────────── */

static int netware_main(void *arg)
{
    (void)arg;

    /* Determine number of CPUs. */
    ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus <= 0)          ncpus = 1;
    if (ncpus > 32)                 ncpus = 32;

    srand((unsigned)time(NULL));

    /* Check for ASCII-only mode (set by --ascii / VLOCK_ASCII).      */
    read_ascii_mode();

    /* Create one worm per CPU and seed them. */
    worms   = calloc((size_t)ncpus, sizeof(worm_t));
    if (!worms) {
        endwin();
        return 1;
    }

    for (int i = 0; i < ncpus; i++)
        new_worm();

    /* Initialise colors. */
    init_colors();

    /* Set stdscr background so clear/eol uses black bg. */
    bkgdset(' ' | COLOR_PAIR(COLOR_MIN));

    while (1) {
        if (signal_status == SIGINT)     break;
        if (signal_status == SIGWINCH) {
            struct winsize ws;
            int fd = open(ttyname(STDOUT_FILENO), O_RDONLY);
            if (fd >= 0 && ioctl(fd, TIOCGWINSZ, &ws) == 0) {
                COLS      = ws.ws_col;
                LINES     = ws.ws_row;
                resizeterm(LINES, COLS);
            }
            close(fd);
        }

        /* Drain input non-blockingly. Parent kills us on wake key. */
        (void)wgetch(stdscr);

        /* Get global CPU utilisation for this frame. */
        float cpu_util  = get_cpu_utilization();

        /* Step every worm. */
        for (int i = 0; i < ncpus; i++) {
            update_worm(&worms[i], cpu_util);
        }

        /* Draw. */
        clear();
        draw_frame();
        info_box_draw();
        wnoutrefresh(stdscr);
        doupdate();

        napms(80);
    }

    cleanup_worms();
    return 0;
}
