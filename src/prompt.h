/* prompt.h -- header file for the prompt routines for vlock,
 *             the VT locking program for linux
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

#pragma once

#include "verror.h"

#define VLOCK_PROMPT_ERROR 3

enum {
  VLOCK_PROMPT_ERROR_FAILED,
  VLOCK_PROMPT_ERROR_TIMEOUT,
};

/* Prompt for a string with the given message.  The string is returned if
 * successfully read, otherwise NULL.  The caller is responsible for freeing
 * the resulting buffer.  If no string is read after the given timeout this
 * prompt() returns NULL.  A timeout of NULL means no timeout, i.e. wait forever.
 */
char *prompt(const char *msg, const struct timespec *timeout, VError **error);

/* Same as prompt() above, except that characters entered are not echoed. */
char *prompt_echo_off(const char *msg,
                      const struct timespec *timeout,
                      VError **error);

/* Read a single character from the stdin.  If the timeout is reached
 * 0 is returned. */
char read_character(const struct timespec *timeout, VError **error);

/* Wait for any of the characters in the given character set to be read from
 * stdin.  If charset is NULL wait for any character.  Returns 0 when the
 * timeout occurs. */
char wait_for_character(const char *charset, const struct timespec *timeout, VError **error);
