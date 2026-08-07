#include <stdio.h>
#include <stdlib.h>

#include "logging.h"

/* Quiet by default.  When VLOCK_DEBUG is set in the environment, debug/info
 * messages may be printed by call sites that check vlock_debug_enabled(). */
static int debug_enabled;

void vlock_initialize_logging(void)
{
  const char *vlock_debug = getenv("VLOCK_DEBUG");

  debug_enabled = (vlock_debug != NULL && *vlock_debug != '\0');
}

int vlock_debug_enabled(void)
{
  return debug_enabled;
}
