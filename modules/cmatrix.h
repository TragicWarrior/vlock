#ifndef _CMATRIX_H_
#define _CMATRIX_H_

typedef struct cmatrix {
    int val;
    int bold;
} cmatrix;


void sighandler(int s);


/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */
/* Define this if your curses library has use_default_colors, for 
   cool transparency =-) */
#define HAVE_USE_DEFAULT_COLORS 1

/* Define this if you have the linux consolechars program */
/* #undef HAVE_CONSOLECHARS */

/* Define this if you have the linux setfont program */
#define HAVE_SETFONT /bin/setfont

/* Define this if you have the wresize function in your ncurses-type library */
#define HAVE_WRESIZE 1

/* Define this if you have the resizeterm function in your ncurses-type library */
#define HAVE_RESIZETERM 1


/* Define to 1 if you have the <curses.h> header file. */
#define HAVE_CURSES_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `ncurses' library (-lncurses). */
#define HAVE_LIBNCURSES 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <ncurses.h> header file. */
#define HAVE_NCURSES_H 1

/* Define to 1 if you have the `putenv' function. */
#define HAVE_PUTENV 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <termios.h> header file. */
#define HAVE_TERMIOS_H 1

/* Define to 1 if you have the <termio.h> header file. */
#define HAVE_TERMIO_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the file `/usr/lib/kbd/consolefonts'. */
/* #undef HAVE__USR_LIB_KBD_CONSOLEFONTS */

/* Define to 1 if you have the file `/usr/lib/X11/fonts/misc'. */
/* #undef HAVE__USR_LIB_X11_FONTS_MISC */

/* Define to 1 if you have the file `/usr/share/consolefonts'. */
#define HAVE__USR_SHARE_CONSOLEFONTS 1

/* Define to 1 if you have the file `/usr/X11R6/lib/X11/fonts/misc'. */
/* #undef HAVE__USR_X11R6_LIB_X11_FONTS_MISC */

/* Name of package */
#define PACKAGE "cmatrix"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "abishekvashok@gmail.com"

/* Define to the full name of this package. */
#define PACKAGE_NAME "cmatrix"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "cmatrix 1.2"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "cmatrix"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.2"

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Version number of package */
#define VERSION "1.2"

#endif
