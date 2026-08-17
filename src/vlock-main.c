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
#include <assert.h>
#include <time.h>


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

#ifdef USE_PLUGINS
/* Interpret an environment variable as a boolean (1/y/yes/true/on). */
static bool env_is_true(const char *name)
{
  const char *v = getenv(name);

  return v != NULL
    && (strcmp(v, "1") == 0 || strcmp(v, "y") == 0 || strcmp(v, "Y") == 0
        || strcmp(v, "yes") == 0 || strcmp(v, "true") == 0
        || strcmp(v, "on") == 0);
}

/* Map VLOCK_WAKE_KEY to the set of characters that dismiss the screen saver.
 * NULL means "any key". */
static const char *wake_key_charset(void)
{
  const char *k = getenv("VLOCK_WAKE_KEY");

  if (k == NULL || strcmp(k, "any") == 0)
    return NULL;
  else if (strcmp(k, "enter") == 0 || strcmp(k, "return") == 0)
    return "\n\r";
  else if (strcmp(k, "space") == 0)
    return " ";
  else if (strcmp(k, "backspace") == 0)
    return "\b\177";
  else
    return NULL;
}
#endif

static void auth_loop(const char *username)
{
  VError *err = NULL;
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
  /* When VLOCK_SAVER is true, start the screen saver plugins immediately
   * instead of waiting for ESC or the timeout. */
  bool saver_only = env_is_true("VLOCK_SAVER");
  /* Which key(s) dismiss the saver (VLOCK_WAKE_KEY); NULL means any key. */
  const char *wake_charset = wake_key_charset();
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

    /* Wait for enter or escape to be pressed.  In saver mode start the screen
     * saver immediately by acting as if ESC had been pressed. */
#ifdef USE_PLUGINS
    if (saver_only)
      c = '\033';
    else
#endif
      c = wait_for_character("\n\r\033", wait_timeout, NULL);

    /* Escape was pressed or the timeout occurred. */
    if (c == '\033' || c == 0) {
#ifdef USE_PLUGINS
      plugin_hook("vlock_save");
      /* Wait for the configured wake key (any key by default). */
      (void) wait_for_character(wake_charset, NULL, NULL);
      plugin_hook("vlock_save_abort");

      /* Any key dismisses the saver and brings up the password prompt. */
#else
      continue;
#endif
    }

    for (size_t i = 0; auth_names[i] != NULL; i++) {
      if (auth(auth_names[i], prompt_timeout, &err))
        goto auth_success;

      assert(err != NULL);

      if (verror_matches(err,
                          VLOCK_PROMPT_ERROR,
                          VLOCK_PROMPT_ERROR_TIMEOUT))
        fprintf(stderr, "Timeout!\n");
      else {
        fprintf(stderr, "vlock: %s\n", err->message);

        if (verror_matches(err,
                            VLOCK_AUTH_ERROR,
                            VLOCK_AUTH_ERROR_FAILED)) {
          fputs(auth_failure_blurb, stderr);
          sleep(3);
        }
      }

      verror_clear(&err);
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


  /* Initialize logging. */
  vlock_initialize_logging();

  install_signal_handlers();

  /* Get the user name from the environment if started as root. */
  if (getuid() == 0)
    username = getenv("USER");

  if (username == NULL) {
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL)
      username = pw->pw_name;
  }

  if (username == NULL) {
    fprintf(stderr, "vlock: could not determine username\n");
    exit(EXIT_FAILURE);
  }

  vlock_atexit(display_auth_tries);

#ifdef USE_PLUGINS
  VError *tmp_error = NULL;

  for (int i = 1; i < argc; i++) {
    if (!load_plugin(argv[i], &tmp_error)) {
      assert(tmp_error != NULL);

      if (verror_matches(tmp_error,
                          VLOCK_PLUGIN_ERROR,
                          VLOCK_PLUGIN_ERROR_NOT_FOUND))
        fprintf(stderr, "vlock: no such plugin '%s'\n", argv[i]);
      else
        fprintf(stderr,
                  "vlock: loading plugin '%s' failed: %s\n",
                  argv[i],
                  tmp_error->message);

      verror_clear(&tmp_error);
      exit(EXIT_FAILURE);
    }
  }

  vlock_atexit(unload_plugins);

  if (!resolve_dependencies(&tmp_error)) {
    assert(tmp_error != NULL);
    fprintf(stderr,
              "vlock: error resolving plugin dependencies: %s\n",
              tmp_error->message);
    verror_clear(&tmp_error);
    exit(EXIT_FAILURE);
  }

  plugin_hook("vlock_start");
  vlock_atexit(call_end_hook);
#else /* !USE_PLUGINS */
  /* Emulate pseudo plugin "all". */
  if (argc == 2 && (strcmp(argv[1], "all") == 0)) {
    if (!lock_console_switch()) {
      if (errno)
        fprintf(stderr,
                  "vlock: could not disable console switching: %s\n",
                  strerror(errno));

      exit(EXIT_FAILURE);
    }

    vlock_atexit((void (*) (void))unlock_console_switch);
  } else if (argc > 1) {
    fprintf(stderr, "vlock: plugin support disabled\n");
    exit(EXIT_FAILURE);
  }
#endif

  if (!isatty(STDIN_FILENO)) {
    fprintf(stderr, "vlock: stdin is not a terminal\n");
    exit(EXIT_FAILURE);
  }

  /* Delay securing the terminal until here because one of the plugins might
   * have changed the active terminal. */
  secure_terminal();
  vlock_atexit(restore_terminal);

  auth_loop(username);

  exit(EXIT_SUCCESS);
}

