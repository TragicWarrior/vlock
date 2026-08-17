/* plugin.h -- header file for the generic plugin routines for vlock,
 *             the VT locking program for linux
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

#pragma once

#include <stdbool.h>
#include "vlist.h"
#include "verror.h"

/* Names of dependencies plugins may specify. */
#define nr_dependencies 6
extern const char *dependency_names[nr_dependencies];

/* A plugin hook consists of a name and a handler function. */
struct hook
{
  const char *name;
  void (*handler)(const char *);
};

/* Hooks that a plugin may define. */
#define nr_hooks 4
extern const struct hook hooks[nr_hooks];

/* Error domain / codes for plugin failures. */
#define VLOCK_PLUGIN_ERROR 1

enum {
  VLOCK_PLUGIN_ERROR_FAILED,
  VLOCK_PLUGIN_ERROR_DEPENDENCY,
  VLOCK_PLUGIN_ERROR_NOT_FOUND
};

typedef struct VlockPlugin VlockPlugin;
typedef struct VlockPluginClass VlockPluginClass;

struct VlockPluginClass
{
  /* Free subtype-specific resources, then free the object. */
  void (*destroy)(VlockPlugin *self);
  bool (*open)(VlockPlugin *self, VError **error);
  bool (*call_hook)(VlockPlugin *self, const char *hook_name);
};

struct VlockPlugin
{
  const VlockPluginClass *klass;

  char *name;

  VList *dependencies[nr_dependencies];

  bool save_disabled;
};

/* Initialize base fields (name, empty deps).  name is copied; slash stripped.
 * Returns false on OOM. */
bool vlock_plugin_init(VlockPlugin *self, const VlockPluginClass *klass,
                       const char *name);

/* Destroy plugin (calls klass->destroy). */
void vlock_plugin_unref(VlockPlugin *self);

/* Open the plugin. */
bool vlock_plugin_open(VlockPlugin *self, VError **error);

VList *vlock_plugin_get_dependencies(VlockPlugin *self,
                                     const char *dependency_name);
bool vlock_plugin_call_hook(VlockPlugin *self, const char *hook_name);
