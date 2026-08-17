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
#include <assert.h>

#include <sys/types.h>

#include "util.h"
#include "plugin.h"
#include "module.h"

static void vlock_module_destroy(VlockPlugin *plugin)
{
  VlockModule *self = (VlockModule *)plugin;

  if (self->dl_handle != NULL) {
    dlclose(self->dl_handle);
    self->dl_handle = NULL;
  }

  free(self);
}

static bool vlock_module_open(VlockPlugin *plugin, VError **error)
{
  VlockModule *self = (VlockModule *)plugin;

  assert(self->dl_handle == NULL);

  char *path = NULL;
  if (asprintf(&path, "%s/%s.so", VLOCK_MODULE_DIR, plugin->name) < 0) {
    verror_set(error, VLOCK_PLUGIN_ERROR, VLOCK_PLUGIN_ERROR_FAILED,
               "could not open module '%s': out of memory", plugin->name);
    return false;
  }

  /* Test for access.  This must be done manually because vlock most likely
   * runs as a setuid executable and would otherwise override restrictions. */
  if (access(path, R_OK) < 0) {
    int error_code = (errno == ENOENT) ?
                     VLOCK_PLUGIN_ERROR_NOT_FOUND :
                     VLOCK_PLUGIN_ERROR_FAILED;

    verror_set(
      error,
      VLOCK_PLUGIN_ERROR,
      error_code,
      "could not open module '%s': %s",
      plugin->name,
      strerror(errno));

    free(path);
    return false;
  }

  /* Open the module as a shared library. */
  void *dl_handle = self->dl_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

  free(path);

  if (dl_handle == NULL) {
    verror_set(
      error,
      VLOCK_PLUGIN_ERROR,
      VLOCK_PLUGIN_ERROR_FAILED,
      "could not open module '%s': %s",
      plugin->name,
      dlerror());

    return false;
  }

  /* Load all the hooks.  Unimplemented hooks are NULL and will not be called
   * later.  Copy the void* from dlsym into the function pointer with memcpy to
   * avoid a strict-aliasing violation. */
  for (size_t i = 0; i < nr_hooks; i++) {
    void *sym = dlsym(dl_handle, hooks[i].name);
    memcpy(&self->hooks[i], &sym, sizeof sym);
  }

  /* Load all dependencies.  Unspecified dependencies are NULL. */
  for (size_t i = 0; i < nr_dependencies; i++) {
    const char *(*dependency)[] = dlsym(dl_handle, dependency_names[i]);

    /* Append array elements to list. */
    for (size_t j = 0; dependency != NULL && (*dependency)[j] != NULL; j++) {
      char *s = strdup((*dependency)[j]);

      if (s == NULL) {
        verror_set(error, VLOCK_PLUGIN_ERROR, VLOCK_PLUGIN_ERROR_FAILED,
                   "could not open module '%s': out of memory", plugin->name);
        return false;
      }

      plugin->dependencies[i] = vlist_append(plugin->dependencies[i], s);
    }
  }

  return true;
}

static bool vlock_module_call_hook(VlockPlugin *plugin, const char *hook_name)
{
  VlockModule *self = (VlockModule *)plugin;

  /* Find the right hook index. */
  for (size_t i = 0; i < nr_hooks; i++)
    if (strcmp(hooks[i].name, hook_name) == 0) {
      module_hook_function hook = self->hooks[i];

      if (hook != NULL)
        return hook(&self->hook_context);
    }

  return true;
}

static const VlockPluginClass vlock_module_class = {
  .destroy = vlock_module_destroy,
  .open = vlock_module_open,
  .call_hook = vlock_module_call_hook,
};

VlockPlugin *vlock_module_new(const char *name)
{
  VlockModule *self = calloc(1, sizeof *self);

  if (self == NULL)
    return NULL;

  if (!vlock_plugin_init(&self->parent, &vlock_module_class, name)) {
    free(self);
    return NULL;
  }

  self->dl_handle = NULL;
  self->hook_context = NULL;
  return &self->parent;
}
