/* module.c -- module routines for vlock, the VT locking program for linux
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

/* Modules are shared objects that are loaded into vlock's address space. */
/* They can define certain functions that are called through vlock's plugin
 * mechanism.  They should also define dependencies if they depend on other
 * plugins of have to be called before or after other plugins. */

#if !defined(__FreeBSD__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

#include <sys/types.h>

#include <glib.h>
#include <glib-object.h>

#include "util.h"

#include "plugin.h"
#include "module.h"

G_DEFINE_TYPE(VlockModule, vlock_module, TYPE_VLOCK_PLUGIN)

#define VLOCK_MODULE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj),\
                                                                   TYPE_VLOCK_MODULE,\
                                                                   VlockModulePrivate))

/* A hook function as defined by a module. */
typedef bool (*module_hook_function)(void **);

struct _VlockModulePrivate
{
  /* Handle returned by dlopen(). */
  void *dl_handle;

  /* Pointer to be used by the module's hooks. */
  void *hook_context;

  /* Array of hook functions befined by a single module.  Stored in the same
   * order as the global hooks. */
  module_hook_function hooks[nr_hooks];
};

static bool vlock_module_open(VlockPlugin *plugin, GError **error)
{
  VlockModule *self = VLOCK_MODULE(plugin);

  g_assert(self->priv->dl_handle == NULL);

  char *path = g_strdup_printf("%s/%s.so", VLOCK_MODULE_DIR, plugin->name);

  /* Test for access.  This must be done manually because vlock most likely
   * runs as a setuid executable and would otherwise override restrictions. */
  if (access(path, R_OK) < 0) {
    gint error_code = (errno == ENOENT) ?
                      VLOCK_PLUGIN_ERROR_NOT_FOUND :
                      VLOCK_PLUGIN_ERROR_FAILED;

    g_set_error(
      error,
      VLOCK_PLUGIN_ERROR,
      error_code,
      "could not open module '%s': %s",
      plugin->name,
      g_strerror(errno));

    g_free(path);
    return false;
  }

  /* Open the module as a shared library. */
  void *dl_handle = self->priv->dl_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

  g_free(path);

  if (dl_handle == NULL) {
    g_set_error(
      error,
      VLOCK_PLUGIN_ERROR,
      VLOCK_PLUGIN_ERROR_FAILED,
      "could not open module '%s': %s",
      plugin->name,
      dlerror());

    return false;
  }

  /* Load all the hooks.  Unimplemented hooks are NULL and will not be called later. */
  for (size_t i = 0; i < nr_hooks; i++)
    *(void **)(&self->priv->hooks[i]) = dlsym(dl_handle, hooks[i].name);

  /* Load all dependencies.  Unspecified dependencies are NULL. */
  for (size_t i = 0; i < nr_dependencies; i++) {
    const char *(*dependency)[] = dlsym(dl_handle, dependency_names[i]);

    /* Append array elements to list. */
    for (size_t j = 0; dependency != NULL && (*dependency)[j] != NULL; j++) {
      char *s = g_strdup((*dependency)[j]);

      plugin->dependencies[i] = g_list_append(plugin->dependencies[i], s);
    }
  }

  return true;
}

static bool vlock_module_call_hook(VlockPlugin *plugin, const gchar *hook_name)
{
  VlockModule *self = VLOCK_MODULE(plugin);

  /* Find the right hook index. */
  for (size_t i = 0; i < nr_hooks; i++)
    if (strcmp(hooks[i].name, hook_name) == 0) {
      module_hook_function hook = self->priv->hooks[i];

      if (hook != NULL)
        return hook(&self->priv->hook_context);
    }

  return true;
}

/* Initialize plugin to default values. */
static void vlock_module_init(VlockModule *self)
{
  self->priv = VLOCK_MODULE_GET_PRIVATE(self);
  self->priv->dl_handle = NULL;
}

/* Destroy module object. */
static void vlock_module_finalize(GObject *object)
{
  VlockModule *self = VLOCK_MODULE(object);

  if (self->priv->dl_handle != NULL) {
    dlclose(self->priv->dl_handle);
    self->priv->dl_handle = NULL;
  }

  G_OBJECT_CLASS(vlock_module_parent_class)->finalize(object);
}

/* Initialize module class. */
static void vlock_module_class_init(VlockModuleClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  VlockPluginClass *plugin_class = VLOCK_PLUGIN_CLASS(klass);

  g_type_class_add_private(klass, sizeof(VlockModulePrivate));

  /* Virtual methods. */
  gobject_class->finalize = vlock_module_finalize;

  plugin_class->open = vlock_module_open;
  plugin_class->call_hook = vlock_module_call_hook;
}

