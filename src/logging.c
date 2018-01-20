#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include "logging.h"

static void vlock_quiet_log_handler(const gchar *log_domain __attribute__((unused)),
				    GLogLevelFlags log_level __attribute__((unused)),
				    const gchar *message __attribute__((unused)),
                                       gpointer user_data __attribute__((unused)))
{
  /* Do nothing ... */
}

void vlock_initialize_logging(void)
{
  const gchar *vlock_debug = g_getenv("VLOCK_DEBUG");

  if (vlock_debug == NULL || *vlock_debug == '\0')
    g_log_set_handler(NULL, G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO, vlock_quiet_log_handler, NULL);

}

