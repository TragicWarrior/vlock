/* auth-shadow.c -- shadow authentification routine for vlock,
 *                  the VT locking program for linux
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

/* for crypt() */
#define _XOPEN_SOURCE

#ifndef __FreeBSD__
/* for asprintf() */
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/mman.h>

#include <shadow.h>

#include "auth.h"
#include "prompt.h"

GQuark vlock_auth_error_quark(void)
{
  return g_quark_from_static_string("vlock-auth-shadow-error-quark");
}

bool auth(const char *user, struct timespec *timeout, GError **error)
{
  char *pwd;
  char *cryptpw;
  char *msg;
  struct spwd *spw;
  int result = false;

  g_return_val_if_fail(error == NULL || *error == NULL, false);

  /* format the prompt */
  if (asprintf(&msg, "%s's Password: ", user) < 0) {
    g_propagate_error(error,
                      g_error_new_literal(
                        VLOCK_AUTH_ERROR,
                        VLOCK_AUTH_ERROR_FAILED,
                        g_strerror(errno)));
    return false;
  }

  if ((pwd = prompt_echo_off(msg, timeout, error)) == NULL)
    goto prompt_error;

  errno = 0;

  /* get the shadow password */
  if ((spw = getspnam(user)) == NULL) {
    if (errno == 0)
      goto auth_error;

    g_set_error(error,
                VLOCK_AUTH_ERROR,
                VLOCK_AUTH_ERROR_FAILED,
                "Could not get shadow record: %s",
                g_strerror(errno));
    goto shadow_error;
  }

  /* hash the password */
  if ((cryptpw = crypt(pwd, spw->sp_pwdp)) == NULL) {
    g_set_error(error,
                VLOCK_AUTH_ERROR,
                VLOCK_AUTH_ERROR_FAILED,
                "crypt() failed: %s",
                g_strerror(errno));
    goto shadow_error;
  }

  result = (strcmp(cryptpw, spw->sp_pwdp) == 0);

  if (!result) {
auth_error:
    sleep(1);
    g_propagate_error(error,
                      g_error_new_literal(
                        VLOCK_AUTH_ERROR,
                        VLOCK_AUTH_ERROR_DENIED,
                        "Authentication failure"));
  }

shadow_error:
  /* deallocate shadow resources */
  endspent();

  /* free the password */
  free(pwd);

prompt_error:
  /* free the prompt */
  free(msg);

  g_assert(result || error != NULL);

  return result;
}

