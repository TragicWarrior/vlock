/* prompt.c -- prompt routines for vlock,
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
 *
 * The prompt functions (prompt and prompt_echo_off) were
 * inspired by/copied from openpam's openpam_ttyconv.c:
 *
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

#include <glib.h>

#include "prompt.h"

#define PROMPT_BUFFER_SIZE 512

GQuark vlock_prompt_error_quark(void)
{
  return g_quark_from_static_string("vlock-prompt-error-quark");
}

/* Prompt with the given string for a single line of input.  The read string is
 * returned in a new buffer that should be freed by the caller.  If reading
 * fails or the timeout (if given) occurs NULL is retured. */
char *prompt(const char *msg, const struct timespec *timeout, GError **error)
{
  GError *err = NULL;
  char buffer[PROMPT_BUFFER_SIZE];
  char *result = NULL;
  size_t len;
  struct termios term;
  tcflag_t lflag;

  if (msg != NULL) {
    /* Write out the prompt. */
    (void) fputs(msg, stderr);
    fflush(stderr);
  }

  /* Get the current terminal attributes. */
  (void) tcgetattr(STDIN_FILENO, &term);
  /* Save the lflag value. */
  lflag = term.c_lflag;
  /* Disable terminal signals. */
  term.c_lflag &= ~ISIG;
  /* Set the terminal attributes. */
  (void) tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);
  /* Discard all unread input characters. */
  (void) tcflush(STDIN_FILENO, TCIFLUSH);

  /* Read the string one character at a time. */
  for (len = 0; len < sizeof buffer - 1; len++) {
    char c = wait_for_character(NULL, timeout, &err);

    if (err != NULL) {
      g_propagate_error(error, err);
      goto out;
    } else if (c == '\n') {
      break;
    }

    buffer[len] = c;
  }

  /* Terminate the string. */
  buffer[len] = '\0';

  /* Copy the string. */
  if ((result = strdup(buffer)) == NULL)
    g_propagate_error(error,
                      g_error_new_literal(
                        VLOCK_PROMPT_ERROR,
                        VLOCK_PROMPT_ERROR_FAILED,
                        g_strerror(errno)));

  /* Clear our buffer. */
  memset(buffer, 0, sizeof buffer);

out:
  /* Restore original terminal attributes. */
  term.c_lflag = lflag;
  (void) tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);

  return result;
}

/* Same as prompt except that the characters entered are not echoed. */
char *prompt_echo_off(const char *msg,
                      const struct timespec *timeout,
                      GError **error)
{
  struct termios term;
  tcflag_t lflag;
  char *result;

  (void) tcgetattr(STDIN_FILENO, &term);
  lflag = term.c_lflag;
  term.c_lflag &= ~ECHO;
  (void) tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);

  result = prompt(msg, timeout, error);

  term.c_lflag = lflag;
  (void) tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);

  if (result != NULL)
    fputc('\n', stderr);

  return result;
}

/* Read a single character from the stdin.  If the timeout is reached
 * 0 is returned. */
char read_character(const struct timespec *timeout, GError **error)
{
  char c = 0;
  struct timeval *timeout_val = NULL;
  fd_set readfds;

  g_assert(error == NULL || *error == NULL);

before_select:
  if (timeout != NULL) {
    timeout_val = calloc(sizeof *timeout_val, 1);

    if (timeout_val != NULL) {
      timeout_val->tv_sec = timeout->tv_sec;
      timeout_val->tv_usec = timeout->tv_nsec / 1000;
    }
  }

  /* Initialize file descriptor set. */
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  /* Reset errno. */
  errno = 0;

  /* Wait for a character. */
  if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, timeout_val) != 1) {
    switch (errno) {
      case EINTR:
	/* A signal was caught.  Restart. */
	free(timeout_val);
	goto before_select;
      case 0:
	/* Timeout was hit. */
	g_propagate_error(error,
			  g_error_new_literal(
                            VLOCK_PROMPT_ERROR,
			    VLOCK_PROMPT_ERROR_TIMEOUT,
			    ""));
	goto out;
      default:
	/* Some other error. */
	g_propagate_error(error,
			  g_error_new_literal(
                            VLOCK_PROMPT_ERROR,
			    VLOCK_PROMPT_ERROR_FAILED,
			    g_strerror(errno)));
	goto out;
    }
  }

  /* Read the character. */
  (void) read(STDIN_FILENO, &c, 1);

out:
  free(timeout_val);
  return c;
}

/* Wait for any of the characters in the given character set to be read from
 * stdin.  If charset is NULL wait for any character.  Returns 0 when the
 * timeout occurs. */
char wait_for_character(const char *charset, const struct timespec *timeout, GError **error)
{
  struct termios term;
  tcflag_t lflag;
  char c;

  /* switch off line buffering */
  (void) tcgetattr(STDIN_FILENO, &term);
  lflag = term.c_lflag;
  term.c_lflag &= ~ICANON;
  (void) tcsetattr(STDIN_FILENO, TCSANOW, &term);

  for (;;) {
    c = read_character(timeout, error);

    if (c == 0 || charset == NULL)
      break;
    else if (strchr(charset, c) != NULL)
      break;
  }

  /* restore line buffering */
  term.c_lflag = lflag;
  (void) tcsetattr(STDIN_FILENO, TCSANOW, &term);

  return c;
}

