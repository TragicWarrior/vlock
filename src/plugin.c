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
#include <assert.h>

#include "plugin.h"
#include "util.h"

/* Initialize base fields.  name is copied; any directory prefix is stripped
 * (plugin names must not contain a slash for security). */
bool vlock_plugin_init(VlockPlugin *self, const VlockPluginClass *klass,
                       const char *name)
{
  assert(self != NULL);
  assert(klass != NULL);
  assert(name != NULL);

  memset(self, 0, sizeof *self);
  self->klass = klass;
  self->save_disabled = false;

  for (size_t i = 0; i < nr_dependencies; i++)
    self->dependencies[i] = NULL;

  /* For security plugin names must not contain a slash. */
  const char *last_slash = strrchr(name, '/');
  if (last_slash != NULL)
    name = last_slash + 1;

  self->name = strdup(name);
  if (self->name == NULL)
    return false;

  return true;
}

void vlock_plugin_unref(VlockPlugin *self)
{
  if (self == NULL)
    return;

  /* Free dependency string lists. */
  for (size_t i = 0; i < nr_dependencies; i++) {
    while (self->dependencies[i] != NULL) {
      free(self->dependencies[i]->data);
      self->dependencies[i] = vlist_delete_link(self->dependencies[i],
                                                self->dependencies[i]);
    }
  }

  free(self->name);
  self->name = NULL;

  if (self->klass != NULL && self->klass->destroy != NULL)
    self->klass->destroy(self);
  else
    free(self);
}

bool vlock_plugin_open(VlockPlugin *self, VError **error)
{
  assert(self != NULL);
  assert(self->klass != NULL);
  assert(self->klass->open != NULL);
  return self->klass->open(self, error);
}

bool vlock_plugin_call_hook(VlockPlugin *self, const char *hook_name)
{
  assert(self != NULL);
  assert(self->klass != NULL);
  assert(self->klass->call_hook != NULL);
  return self->klass->call_hook(self, hook_name);
}

VList *vlock_plugin_get_dependencies(VlockPlugin *self,
                                     const char *dependency_name)
{
  for (size_t i = 0; i < nr_dependencies; i++)
    if (strcmp(dependency_names[i], dependency_name) == 0)
      return self->dependencies[i];
  return NULL;
}
