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
#include <glib.h>
#include <glib-object.h>

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

/* Errors */
#define VLOCK_PLUGIN_ERROR vlock_plugin_error_quark()
GQuark vlock_plugin_error_quark(void);

enum {
  VLOCK_PLUGIN_ERROR_FAILED,
  VLOCK_PLUGIN_ERROR_DEPENDENCY,
  VLOCK_PLUGIN_ERROR_NOT_FOUND
};

/*
 * Plugin type macros.
 */
#define TYPE_VLOCK_PLUGIN (vlock_plugin_get_type())
#define VLOCK_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_VLOCK_PLUGIN,\
                                                      VlockPlugin))
#define VLOCK_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),\
                                                           TYPE_VLOCK_PLUGIN,\
                                                           VlockPluginClass))
#define IS_VLOCK_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                                         TYPE_VLOCK_PLUGIN))
#define IS_VLOCK_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                                              TYPE_VLOCK_PLUGIN))
#define VLOCK_PLUGIN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),\
                                                               TYPE_VLOCK_PLUGIN,\
                                                               VlockPluginClass))

typedef struct _VlockPlugin VlockPlugin;
typedef struct _VlockPluginClass VlockPluginClass;

struct _VlockPlugin
{
  GObject parent_instance;

  gchar *name;

  GList *dependencies[nr_dependencies];

  bool save_disabled;
};

struct _VlockPluginClass
{
  GObjectClass parent_class;

  bool (*open)(VlockPlugin *self, GError **error);
  bool (*call_hook)(VlockPlugin *self, const gchar *hook_name);
};

GType vlock_plugin_get_type(void);

/* Open the plugin. */
bool vlock_plugin_open(VlockPlugin *self, GError **error);

GList *vlock_plugin_get_dependencies(VlockPlugin *self,
                                     const gchar *dependency_name);
bool vlock_plugin_call_hook(VlockPlugin *self, const gchar *hook_name);
