/* auth.h -- generic header file for authentification routines
 *           for vlock, the VT locking program for linux
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

#include <stdbool.h>
#include <glib.h>

/* forward declaration */
struct timespec;

#define VLOCK_AUTH_ERROR vlock_auth_error_quark()
GQuark vlock_auth_error_quark(void);

enum {
  VLOCK_AUTH_ERROR_FAILED,
  VLOCK_AUTH_ERROR_DENIED
};

/* Try to authenticate the user.  When the user is successfully authenticated
 * this function returns true.  When the authentication fails for whatever
 * reason the function returns false.  The timeout is passed to the prompt
 * functions below if they are called.
 */
bool auth(const char *user, struct timespec *timeout, GError **error);
