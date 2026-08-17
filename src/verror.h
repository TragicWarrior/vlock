/* verror.h -- simple error object (libc replacement for GError)
 *
 * Domains are plain integers (enum values in the owning headers), not GQuarks.
 * Messages are heap-allocated C strings.
 */

#pragma once

#include <stdbool.h>
#include <stdarg.h>

typedef struct VError VError;

struct VError {
  int domain;
  int code;
  char *message;
};

/* Set *error to a new VError (or no-op if error is NULL / *error already set).
 * Returns false so call sites can `return verror_set(...)`. */
bool verror_set(VError **error, int domain, int code, const char *fmt, ...)
  __attribute__((format(printf, 4, 5)));

bool verror_set_literal(VError **error, int domain, int code, const char *message);

/* Move err into *dest (clears err).  No-op if dest is NULL (frees err). */
void verror_propagate(VError **dest, VError *err);

/* True if err is non-NULL and matches domain+code. */
bool verror_matches(const VError *err, int domain, int code);

/* Free and null out. */
void verror_free(VError *err);
void verror_clear(VError **error);
