/* plugin.c -- generic plugin routines for vlock,
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include "plugin.h"
#include "util.h"

GQuark vlock_plugin_error_quark(void)
{
  return g_quark_from_static_string("vlock-plugin-error-quark");
}

G_DEFINE_TYPE(VlockPlugin, vlock_plugin, G_TYPE_OBJECT)

/* Initialize plugin to default values. */
static void vlock_plugin_init(VlockPlugin *self)
{
  self->name = NULL;
  self->save_disabled = false;
  for (size_t i = 0; i < nr_dependencies; i++)
    self->dependencies[i] = NULL;
}

/* Create new plugin object. */
static GObject *vlock_plugin_constructor(GType gtype,
                                         guint n_properties,
                                         GObjectConstructParam *properties)
{
  GObjectClass *parent_class = G_OBJECT_CLASS(vlock_plugin_parent_class);
  GObject *object = parent_class->constructor(gtype, n_properties, properties);
  VlockPlugin *self = VLOCK_PLUGIN(object);

  g_return_val_if_fail(self->name != NULL, NULL);

  return object;
}

/* Destroy plugin object. */
static void vlock_plugin_finalize(GObject *object)
{
  VlockPlugin *self = VLOCK_PLUGIN(object);

  g_free(self->name);
  self->name = NULL;

  /* Destroy dependency lists. */
  for (size_t i = 0; i < nr_dependencies; i++) {
    while (self->dependencies[i] != NULL) {
      g_free(self->dependencies[i]->data);
      self->dependencies[i] = g_list_delete_link(self->dependencies[i],
                                                 self->dependencies[i]);
    }
  }

  G_OBJECT_CLASS(vlock_plugin_parent_class)->finalize(object);
}

/* Properties. */
enum {
  PROP_VLOCK_PLUGIN_0,
  PROP_VLOCK_PLUGIN_NAME
};

static void vlock_plugin_set_name(VlockPlugin *self, const gchar *name)
{
  /* For security plugin names must not contain a slash. */
  char *last_slash = strrchr(name, '/');

  if (last_slash != NULL)
    name = last_slash+1;

  self->name = g_strdup(name);
}

/* Set properties. */
static void vlock_plugin_set_property(GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  VlockPlugin *self = VLOCK_PLUGIN(object);

  switch (property_id)
  {
    case PROP_VLOCK_PLUGIN_NAME:
      g_free(self->name);
      vlock_plugin_set_name(self, g_value_get_string(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

/* Get properties. */
static void vlock_plugin_get_property(GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  VlockPlugin *self = VLOCK_PLUGIN(object);

  switch (property_id)
  {
    case PROP_VLOCK_PLUGIN_NAME:
      g_value_set_string(value, self->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

/* Initialize plugin class. */
static void vlock_plugin_class_init(VlockPluginClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GParamSpec *vlock_plugin_param_spec;

  /* Virtual methods. */
  klass->open = NULL;
  klass->call_hook = NULL;

  /* Install overridden methods. */
  gobject_class->constructor = vlock_plugin_constructor;
  gobject_class->finalize = vlock_plugin_finalize;
  gobject_class->set_property = vlock_plugin_set_property;
  gobject_class->get_property = vlock_plugin_get_property;

  /* Install properties. */
  vlock_plugin_param_spec = g_param_spec_string(
    "name",
    "plugin name",
    "Set the plugin's name",
    NULL,
    G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

  g_object_class_install_property(
    gobject_class,
    PROP_VLOCK_PLUGIN_NAME,
    vlock_plugin_param_spec
    );
}

bool vlock_plugin_open(VlockPlugin *self, GError **error)
{
  VlockPluginClass *klass = VLOCK_PLUGIN_GET_CLASS(self);
  g_assert(klass->open != NULL);
  return klass->open(self, error);
}

bool vlock_plugin_call_hook(VlockPlugin *self, const gchar *hook_name)
{
  VlockPluginClass *klass = VLOCK_PLUGIN_GET_CLASS(self);
  g_assert(klass->call_hook != NULL);
  return klass->call_hook(self, hook_name);
}

