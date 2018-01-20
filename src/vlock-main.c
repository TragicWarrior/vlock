/* vlock-main.c -- main routine for vlock,
 *                    the VT locking program for linux
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <pwd.h>

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>

#include <glib.h>
#include <glib/gprintf.h>
#include <glib-object.h>

#include "prompt.h"
#include "auth.h"
#include "console_switch.h"
#include "signals.h"
#include "terminal.h"
#include "util.h"
#include "logging.h"

#ifdef USE_PLUGINS
#include "plugins.h"
#include "plugin.h"
#endif

static const char *auth_failure_blurb =
  "\n"
  "******************************************************************\n"
  "*** You may not be able to able to unlock your terminal now.   ***\n"
  "***                                                            ***\n"
  "*** Log into another terminal and kill the vlock-main process. ***\n"
  "******************************************************************\n"
  "\n"
;

static int auth_tries;

static void auth_loop(const char *username)
{
  GError *err = NULL;
  struct timespec *prompt_timeout;
  struct timespec *wait_timeout;
  char *vlock_message;
  const char *auth_names[] = { username, "root", NULL };

  /* If NO_ROOT_PASS is defined or the username is "root" ... */
#ifndef NO_ROOT_PASS
  if (strcmp(username, "root") == 0)
#endif
  /* ... do not fall back to "root". */
  auth_names[1] = NULL;

  /* Get the vlock message from the environment. */
  vlock_message = getenv("VLOCK_MESSAGE");

  if (vlock_message == NULL) {
    if (console_switch_locked)
      vlock_message = getenv("VLOCK_ALL_MESSAGE");
    else
      vlock_message = getenv("VLOCK_CURRENT_MESSAGE");
  }

  /* Get the timeouts from the environment. */
  prompt_timeout = parse_seconds(getenv("VLOCK_PROMPT_TIMEOUT"));
#ifdef USE_PLUGINS
  wait_timeout = parse_seconds(getenv("VLOCK_TIMEOUT"));
#else
  wait_timeout = NULL;
#endif

  for (;;) {
    char c;

    /* Print vlock message if there is one. */
    if (vlock_message && *vlock_message) {
      fputs(vlock_message, stderr);
      fputc('\n', stderr);
    }

    /* Wait for enter or escape to be pressed. */
    c = wait_for_character("\n\033", wait_timeout, NULL);

    /* Escape was pressed or the timeout occurred. */
    if (c == '\033' || c == 0) {
#ifdef USE_PLUGINS
      plugin_hook("vlock_save");
      /* Wait for any key to be pressed. */
      c = wait_for_character(NULL, NULL, NULL);
      plugin_hook("vlock_save_abort");

      /* Do not require enter to be pressed twice. */
      if (c != '\n')
        continue;
#else
      continue;
#endif
    }

    for (size_t i = 0; auth_names[i] != NULL; i++) {
      if (auth(auth_names[i], prompt_timeout, &err))
        goto auth_success;

      g_assert(err != NULL);

      if (g_error_matches(err,
                          VLOCK_PROMPT_ERROR,
                          VLOCK_PROMPT_ERROR_TIMEOUT))
        fprintf(stderr, "Timeout!\n");
      else {
        fprintf(stderr, "vlock: %s\n", err->message);

        if (g_error_matches(err,
                            VLOCK_AUTH_ERROR,
                            VLOCK_AUTH_ERROR_FAILED)) {
          fputs(auth_failure_blurb, stderr);
          sleep(3);
        }
      }

      g_clear_error(&err);
      sleep(1);
    }

    auth_tries++;
  }

auth_success:
  /* Free timeouts memory. */
  free(wait_timeout);
  free(prompt_timeout);
}

void display_auth_tries(void)
{
  if (auth_tries > 0)
    fprintf(stderr,
            "%d failed authentication %s.\n",
            auth_tries,
            auth_tries > 1 ? "tries" : "try");
}

#ifdef USE_PLUGINS
static void call_end_hook(void)
{
  (void) plugin_hook("vlock_end");
}

#endif

/* Lock the current terminal until proper authentication is received. */
int main(int argc, char *const argv[])
{
  const char *username = NULL;

  /* Initialize GLib. */
  g_set_prgname(argv[0]);
  g_type_init();

  /* Initialize logging. */
  vlock_initialize_logging();

  install_signal_handlers();

  /* Get the user name from the environment if started as root. */
  if (getuid() == 0)
    username = g_getenv("USER");

  if (username == NULL)
    username = g_get_user_name();

  vlock_atexit(display_auth_tries);

#ifdef USE_PLUGINS
  GError *tmp_error = NULL;

  for (int i = 1; i < argc; i++) {
    if (!load_plugin(argv[i], &tmp_error)) {
      g_assert(tmp_error != NULL);

      if (g_error_matches(tmp_error,
                          VLOCK_PLUGIN_ERROR,
                          VLOCK_PLUGIN_ERROR_NOT_FOUND))
        g_fprintf(stderr, "vlock: no such plugin '%s'\n", argv[i]);
      else
        g_fprintf(stderr,
                  "vlock: loading plugin '%s' failed: %s\n",
                  argv[i],
                  tmp_error->message);

      g_clear_error(&tmp_error);
      exit(EXIT_FAILURE);
    }
  }

  vlock_atexit(unload_plugins);

  if (!resolve_dependencies(&tmp_error)) {
    g_assert(tmp_error != NULL);
    g_fprintf(stderr,
              "vlock: error resolving plugin dependencies: %s\n",
              tmp_error->message);
    g_clear_error(&tmp_error);
    exit(EXIT_FAILURE);
  }

  plugin_hook("vlock_start");
  vlock_atexit(call_end_hook);
#else /* !USE_PLUGINS */
  /* Emulate pseudo plugin "all". */
  if (argc == 2 && (strcmp(argv[1], "all") == 0)) {
    if (!lock_console_switch()) {
      if (errno)
        g_fprintf(stderr,
                  "vlock: could not disable console switching: %s\n",
                  g_strerror(errno));

      exit(EXIT_FAILURE);
    }

    vlock_atexit((void (*) (void))unlock_console_switch);
  } else if (argc > 1) {
    g_fprintf(stderr, "vlock: plugin support disabled\n");
    exit(EXIT_FAILURE);
  }
#endif

  if (!isatty(STDIN_FILENO)) {
    g_fprintf(stderr, "vlock: stdin is not a terminal\n");
    exit(EXIT_FAILURE);
  }

  /* Delay securing the terminal until here because one of the plugins might
   * have changed the active terminal. */
  secure_terminal();
  vlock_atexit(restore_terminal);

  auth_loop(username);

  exit(EXIT_SUCCESS);
}

