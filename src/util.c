/* util.c -- utility routines for vlock, the VT locking program for linux
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

#if !defined(__FreeBSD__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <glib.h>

#include "util.h"

/* Parse the given string (interpreted as seconds) into a
 * timespec.  On error NULL is returned.  The caller is responsible
 * to free the result.   The argument may be NULL, in which case NULL
 * is returned, too.  "0" is also parsed as NULL. */
struct timespec *parse_seconds(const char *s)
{
  if (s == NULL)
    return NULL;
  else {
    char *n;
    struct timespec *t = calloc(sizeof *t, 1);

    if (t == NULL)
      return NULL;

    t->tv_sec = strtol(s, &n, 10);

    if (*n != '\0' || t->tv_sec <= 0) {
      free(t);
      t = NULL;
    }

    return t;
  }
}

static GList *atexit_functions;

typedef union
{
  void *as_pointer;
  void (*as_function)(void);
} function_pointer;

void vlock_invoke_atexit(void)
{
  while (atexit_functions != NULL) {
    function_pointer p = { .as_pointer = atexit_functions->data };
    p.as_function();
    atexit_functions = g_list_delete_link(atexit_functions,
                                          atexit_functions);
  }
}

void vlock_atexit(void (*function)(void))
{
  if (atexit_functions == NULL)
    atexit(vlock_invoke_atexit);

  function_pointer p = { .as_function = function };

  atexit_functions = g_list_prepend(atexit_functions, p.as_pointer);
}

