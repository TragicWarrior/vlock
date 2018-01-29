/* 
    cmatrix.c

    Copyright (C) 1999-2017 Chris Allegretta
    Copyright (C) 2017-Present Abishek V Ashok

    This file is part of cmatrix.

    cmatrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cmatrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar. If not, see <http://www.gnu.org/licenses/>.

*/

/*
    Ported to vlock by Bryan Christ <bryan.christ@gmail.com> on
    January 28, 2018.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <ncurses.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "cmatrix.h"
#include "process.h"
#include "vlock_plugin.h"


static int cmatrix_main(void *argument);

/* Global variables */
int console = 0;
int xwindow = 0;
cmatrix **matrix = (cmatrix **) NULL;
int *length = NULL;  /* Length of cols in each line */
int *spaces = NULL;  /* Spaces left to fill */
int *updates = NULL; /* What does this do again? */
volatile sig_atomic_t signal_status = 0; /* Indicates a caught signal */


bool vlock_save(void **ctx_ptr)
{
    static struct child_process cmatrix_proc = {
        .function = cmatrix_main,
        .argument = NULL,
        .stdin_fd = REDIRECT_DEV_NULL,
        .stdout_fd = NO_REDIRECT,
        .stderr_fd = NO_REDIRECT,
    };

    initscr();
    savetty();
    nonl();
    cbreak();
    noecho();
    timeout(0);
    leaveok(stdscr, TRUE);
    curs_set(0);
    signal(SIGINT, sighandler);
    signal(SIGWINCH, sighandler);

    GError *tmp_error = NULL;

    if (!create_child(&cmatrix_proc, &tmp_error))
        return false;

    *ctx_ptr = &cmatrix_proc;

    return true;
}

bool
vlock_save_abort(void **ctx_ptr)
{
    struct child_process  *train_proc = *ctx_ptr;

    if (train_proc != NULL)
    {
        ensure_death(train_proc->pid);

        /* Restore sane terminal and uninitialize ncurses. */
        curs_set(1);
        clear();
        refresh();
        resetty();
        endwin();

        *ctx_ptr = NULL;
    }

  return true;
}


void *nmalloc(size_t howmuch) {
    void *r;

    r = malloc(howmuch);

    return r;
}

/* Initialize the global variables */
void var_init(void) {
    int i, j;

    if (matrix != NULL) {
        free(matrix[0]);
        free(matrix);
    }

    matrix = nmalloc(sizeof(cmatrix *) * (LINES + 1));
    matrix[0] = nmalloc(sizeof(cmatrix) * (LINES + 1) * COLS);
    for (i = 1; i <= LINES; i++) {
        matrix[i] = matrix[i - 1] + COLS;
    }

    if (length != NULL) {
        free(length);
    }
    length = nmalloc(COLS * sizeof(int));

    if (spaces != NULL) {
        free(spaces);
    }
    spaces = nmalloc(COLS* sizeof(int));

    if (updates != NULL) {
        free(updates);
    }
    updates = nmalloc(COLS * sizeof(int));

    /* Make the matrix */
    for (i = 0; i <= LINES; i++) {
        for (j = 0; j <= COLS - 1; j += 2) {
            matrix[i][j].val = -1;
            /* I couldn't quite get how the bold attribute is used in the code,
             * but it is used uninitialized later on (according to Valgrind and
             * my manual inspection). I guess the default value should be 0,
             * as setting it does not change the observable behaviour and shuts
             * Valgrind up. Also, the code seems to expect a value of 0,
             * although I'm not quite sure about that.
             * In any case, there is an uninitialized use,
             * so some default value should be set here (whatever it is).
             */
            matrix[i][j].bold = 0;
        }
    }

    for (j = 0; j <= COLS - 1; j += 2) {
        /* Set up spaces[] array of how many spaces to skip */
        spaces[j] = (int) rand() % LINES + 1;

        /* And length of the stream */
        length[j] = (int) rand() % (LINES - 3) + 3;

        /* Sentinel value for creation of new objects */
        matrix[1][j].val = ' ';

        /* And set updates[] array for update speed. */
        updates[j] = (int) rand() % 3 + 1;
    }

}

void sighandler(int s) {
    signal_status = s;
}

void resize_screen(void) {
    char *tty;
    int fd = 0;
    int result = 0;
    struct winsize win;

    tty = ttyname(STDOUT_FILENO);
    if (!tty) {
        exit(0);
    }

    fd = open(tty, O_RDONLY);
    if (fd == -1) {
        exit(0);
    }

    result = ioctl(fd, TIOCGWINSZ, &win);
    if (result == -1) {
        exit(0);
    }
    close(fd);


    COLS = win.ws_col;
    LINES = win.ws_row;

    if(LINES <10){
        LINES = 10;
    }
    if(COLS <10){
        COLS = 10;
    }

    // assume these exist
    resizeterm(LINES, COLS);
    if (wresize(stdscr, LINES, COLS) == ERR) {
        return;
    }
    var_init();

    /* Do these because width may have changed... */
    clear();
    refresh();
}

int cmatrix_main(void *argument) {

    // int i, y, z, optchr, keypress;
    int i, y, z, keypress;
    int j = 0;
    int count = 0;
    int screensaver = 0;
    int asynch = 0;
    int bold = -1;
    int force = 0;
    int firstcoldone = 0;
    int oldstyle = 0;
    int random = 0;
    int update = 4;
    int highnum = 0;
    int mcolor = COLOR_GREEN;
    int rainbow = 0;    
    int randnum = 0;
    int randmin = 0;
    int pause = 0;

    char *oldtermname;
    char *syscmd = NULL;

    time_t t;
    srand((unsigned) time(&t));

    // supress compiler warning for now
    (void)argument;

    /* Many thanks to morph- (morph@jmss.com) for this getopt patch */

/*    opterr = 0;
    while ((optchr = getopt(argc, argv, "abBfhlnrosxVu:C:")) != EOF) {
        switch (optchr) {
        case 's':
            screensaver = 1;
            break;
        case 'a':
            asynch = 1;
            break;
        case 'b':
            if (bold != 2 && bold != 0) {
                bold = 1;
            }
            break;
        case 'B':
            if (bold != 0) {
                bold = 2;
            }
            break;
        case 'C':
            if (!strcasecmp(optarg, "green")) {
                mcolor = COLOR_GREEN;
            } else if (!strcasecmp(optarg, "red")) {
                mcolor = COLOR_RED;
            } else if (!strcasecmp(optarg, "blue")) {
                mcolor = COLOR_BLUE;
            } else if (!strcasecmp(optarg, "white")) {
                mcolor = COLOR_WHITE;
            } else if (!strcasecmp(optarg, "yellow")) {
                mcolor = COLOR_YELLOW;
            } else if (!strcasecmp(optarg, "cyan")) {
                mcolor = COLOR_CYAN;
            } else if (!strcasecmp(optarg, "magenta")) {
                mcolor = COLOR_MAGENTA;
            } else if (!strcasecmp(optarg, "black")) {
                mcolor = COLOR_BLACK;
            } else {
                printf(" Invalid color selection\n Valid "
                       "colors are green, red, blue, "
                       "white, yellow, cyan, magenta " "and black.\n");
                exit(1);
            }
            break;
        case 'f':
            force = 1;
            break;
        case 'l':
            console = 1;
            break;
        case 'n':
            bold = 0;
            break;
        case 'h':
        case '?':
            usage();
            exit(0);
        case 'o':
            oldstyle = 1;
            break;
        case 'u':
            update = atoi(optarg);
            break;
        case 'x':
            xwindow = 1;
            break;
        case 'V':
            version();
            exit(0);
        case 'r':
             rainbow = 1;
             break;
        }
    }
*/

    /* If bold hasn't been turned on or off yet, assume off */
    if (bold == -1) {
        bold = 0;
    }

    oldtermname = getenv("TERM");
    if (force && strcmp("linux", getenv("TERM"))) {
        /* Portability wins out here, apparently putenv is much more
           common on non-Linux than setenv */
        putenv("TERM=linux");
    }

    if (has_colors()) {
        start_color();
        /* Add in colors, if available */
#ifdef HAVE_USE_DEFAULT_COLORS
        if (use_default_colors() != ERR) {
            init_pair(COLOR_BLACK, -1, -1);
            init_pair(COLOR_GREEN, COLOR_GREEN, -1);
            init_pair(COLOR_WHITE, COLOR_WHITE, -1);
            init_pair(COLOR_RED, COLOR_RED, -1);
            init_pair(COLOR_CYAN, COLOR_CYAN, -1);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, -1);
            init_pair(COLOR_BLUE, COLOR_BLUE, -1);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, -1);
        } else {
#else
        { /* Hack to deal the after effects of else in HAVE_USE_DEFAULT_COLOURS*/
#endif
            init_pair(COLOR_BLACK, COLOR_BLACK, COLOR_BLACK);
            init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
            init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
            init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
            init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
            init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
        }
    }

    srand(time(NULL));

    /* Set up values for random number generation */
    if (console || xwindow) {
        randnum = 51;
        randmin = 166;
        highnum = 217;
    } else {
        randnum = 93;
        randmin = 33;
        highnum = 123;
    }

    var_init();

    while (1) {
        /* Check for signals */
        if (signal_status == SIGINT) {
            // finish();
            /* exits */
            return 0;
        }

        if (signal_status == SIGWINCH) {
            resize_screen();
            signal_status = 0;
        }

        count++;
        if (count > 4) {
            count = 1;
        }

        if ((keypress = wgetch(stdscr)) != ERR) {
            if (screensaver == 1) {
                // finish();
                return 0;
            } else {
                switch (keypress) {
                case 'q':
                    // finish();
                    return 0;
                    break;
                case 'a':
                    asynch = 1 - asynch;
                    break;
                case 'b':
                    bold = 1;
                    break;
                case 'B':
                    bold = 2;
                    break;
                case 'n':
                    bold = 0;
                    break;
                case '0': /* Fall through */
                case '1': /* Fall through */
                case '2': /* Fall through */
                case '3': /* Fall through */
                case '4': /* Fall through */
                case '5': /* Fall through */
                case '6': /* Fall through */
                case '7': /* Fall through */
                case '8': /* Fall through */
                case '9':
                    update = keypress - 48;
                    break;
                case '!':
                    mcolor = COLOR_RED;
                    rainbow = 0;
                    break;
                case '@':
                    mcolor = COLOR_GREEN;
                    rainbow = 0;
                    break;
                case '#':
                    mcolor = COLOR_YELLOW;
                    rainbow = 0;
                    break;
                case '$':
                    mcolor = COLOR_BLUE;
                    rainbow = 0;
                    break;
                case '%':
                    mcolor = COLOR_MAGENTA;
                    rainbow = 0;
                    break;
                case 'r':
                     rainbow = 1;
                     break;
                case '^':
                    mcolor = COLOR_CYAN;
                    rainbow = 0;
                    break;
                case '&':
                    mcolor = COLOR_WHITE;
                    rainbow = 0;
                    break;
                case 'p':
                case 'P':
                    pause = (pause == 0)?1:0;
                    break;

                }
            }
        }
        for (j = 0; j <= COLS - 1; j += 2) {
            if ((count > updates[j] || asynch == 0) && pause == 0) {

                /* I dont like old-style scrolling, yuck */
                if (oldstyle) {
                    for (i = LINES - 1; i >= 1; i--) {
                        matrix[i][j].val = matrix[i - 1][j].val;
                    }
                    random = (int) rand() % (randnum + 8) + randmin;

                    if (matrix[1][j].val == 0) {
                        matrix[0][j].val = 1;
                    } else if (matrix[1][j].val == ' '
                             || matrix[1][j].val == -1) {
                        if (spaces[j] > 0) {
                            matrix[0][j].val = ' ';
                            spaces[j]--;
                        } else {

                            /* Random number to determine whether head of next collumn
                               of chars has a white 'head' on it. */

                            if (((int) rand() % 3) == 1) {
                                matrix[0][j].val = 0;
                            } else {
                                matrix[0][j].val = (int) rand() % randnum + randmin;
                            }
                            spaces[j] = (int) rand() % LINES + 1;
                        }
                    } else if (random > highnum && matrix[1][j].val != 1) {
                        matrix[0][j].val = ' ';
                    } else {
                        matrix[0][j].val = (int) rand() % randnum + randmin;
                    }

                } else { /* New style scrolling (default) */
                    if (matrix[0][j].val == -1 && matrix[1][j].val == ' '
                        && spaces[j] > 0) {
                        matrix[0][j].val = -1;
                        spaces[j]--;
                    } else if (matrix[0][j].val == -1
                        && matrix[1][j].val == ' ') {
                        length[j] = (int) rand() % (LINES - 3) + 3;
                        matrix[0][j].val = (int) rand() % randnum + randmin;

                        if ((int) rand() % 2 == 1) {
                            matrix[0][j].bold = 2;
                        }

                        spaces[j] = (int) rand() % LINES + 1;
                    }
                    i = 0;
                    y = 0;
                    firstcoldone = 0;
                    while (i <= LINES) {

                        /* Skip over spaces */
                        while (i <= LINES && (matrix[i][j].val == ' ' ||
                               matrix[i][j].val == -1)) {
                            i++;
                        }

                        if (i > LINES) {
                            break;
                        }

                        /* Go to the head of this collumn */
                        z = i;
                        y = 0;
                        while (i <= LINES && (matrix[i][j].val != ' ' &&
                               matrix[i][j].val != -1)) {
                            i++;
                            y++;
                        }

                        if (i > LINES) {
                            matrix[z][j].val = ' ';
                            matrix[LINES][j].bold = 1;
                            continue;
                        }

                        matrix[i][j].val = (int) rand() % randnum + randmin;

                        if (matrix[i - 1][j].bold == 2) {
                            matrix[i - 1][j].bold = 1;
                            matrix[i][j].bold = 2;
                        }

                        /* If we're at the top of the collumn and it's reached its
                           full length (about to start moving down), we do this
                           to get it moving.  This is also how we keep segments not
                           already growing from growing accidentally =>
                         */
                        if (y > length[j] || firstcoldone) {
                            matrix[z][j].val = ' ';
                            matrix[0][j].val = -1;
                        }
                        firstcoldone = 1;
                        i++;
                    }
                }
            }
            /* A simple hack */
            if (!oldstyle) {
                y = 1;
                z = LINES;
            } else {
                y = 0;
                z = LINES - 1;
            }
            for (i = y; i <= z; i++) {
                move(i - y, j);

                if (matrix[i][j].val == 0 || matrix[i][j].bold == 2) {
                    if (console || xwindow) {
                        attron(A_ALTCHARSET);
                    }
                    attron(COLOR_PAIR(COLOR_WHITE));
                    if (bold) {
                        attron(A_BOLD);
                    }
                    if (matrix[i][j].val == 0) {
                        if (console || xwindow) {
                            addch(183);
                        } else { 
                            addch('&');
                        }
                    } else {
                        addch(matrix[i][j].val);
                    }

                    attroff(COLOR_PAIR(COLOR_WHITE));
                    if (bold) {
                        attroff(A_BOLD);
                    }
                    if (console || xwindow) {
                        attroff(A_ALTCHARSET);
                    }
                } else {

                    if(rainbow){ 
                        int randomColor = rand() % 6;

                        switch(randomColor){
                            case 0:
                                mcolor = COLOR_GREEN;
                                break;
                            case 1: 
                                mcolor = COLOR_BLUE;
                                break;
                            case 2: 
                                mcolor = COLOR_BLACK;
                                break;
                            case 3:
                                mcolor = COLOR_YELLOW;
                                break;
                            case 4:
                                mcolor = COLOR_CYAN;
                                break;
                            case 5: 
                                mcolor = COLOR_MAGENTA;
                                break;
                       }
                    }
                    attron(COLOR_PAIR(mcolor));
                    if (matrix[i][j].val == 1) {
                        if (bold) {
                            attron(A_BOLD);
                        }
                        addch('|');
                        if (bold) {
                            attroff(A_BOLD);
                        }
                    } else {
                        if (console || xwindow) {
                            attron(A_ALTCHARSET);
                        }
                        if (bold == 2 ||
                            (bold == 1 && matrix[i][j].val % 2 == 0)) {
                            attron(A_BOLD);
                        }
                        if (matrix[i][j].val == -1) {
                            addch(' ');
                        } else {
                            addch(matrix[i][j].val);
                        }
                        if (bold == 2 ||
                            (bold == 1 && matrix[i][j].val % 2 == 0)) {
                            attroff(A_BOLD);
                        }
                        if (console || xwindow) {
                            attroff(A_ALTCHARSET);
                        }
                    }
                    attroff(COLOR_PAIR(mcolor));
                }
            }
        }
        napms(update * 10);
    }
    syscmd = nmalloc(sizeof (char *) * (strlen(oldtermname) + 15));
    sprintf(syscmd, "putenv TERM=%s", oldtermname);    
    system(syscmd);
    // finish();
    return 0;
}

