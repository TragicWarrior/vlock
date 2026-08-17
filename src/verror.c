/* verror.c -- simple error object (libc replacement for GError) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "verror.h"

static VError *verror_new_v(int domain, int code, const char *fmt, va_list ap)
{
  VError *e = malloc(sizeof *e);

  if (e == NULL)
    return NULL;

  e->domain = domain;
  e->code = code;
  e->message = NULL;

  if (fmt != NULL) {
    if (vasprintf(&e->message, fmt, ap) < 0)
      e->message = NULL;
  }

  if (e->message == NULL) {
    /* Fall back so callers always have something printable. */
    e->message = strdup(fmt ? fmt : "unknown error");
    if (e->message == NULL) {
      free(e);
      return NULL;
    }
  }

  return e;
}

bool verror_set(VError **error, int domain, int code, const char *fmt, ...)
{
  if (error == NULL || *error != NULL)
    return false;

  va_list ap;
  va_start(ap, fmt);
  *error = verror_new_v(domain, code, fmt, ap);
  va_end(ap);
  return false;
}

bool verror_set_literal(VError **error, int domain, int code, const char *message)
{
  return verror_set(error, domain, code, "%s", message ? message : "unknown error");
}

void verror_propagate(VError **dest, VError *err)
{
  if (dest == NULL) {
    verror_free(err);
    return;
  }

  if (*dest != NULL) {
    /* Keep the first error; drop the new one (GError-like). */
    verror_free(err);
    return;
  }

  *dest = err;
}

bool verror_matches(const VError *err, int domain, int code)
{
  return err != NULL && err->domain == domain && err->code == code;
}

void verror_free(VError *err)
{
  if (err == NULL)
    return;
  free(err->message);
  free(err);
}

void verror_clear(VError **error)
{
  if (error == NULL || *error == NULL)
    return;
  verror_free(*error);
  *error = NULL;
}
