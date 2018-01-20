/* plugins.c -- plugins for vlock, the VT locking program for linux
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <glib.h>

#include "plugins.h"

#include "tsort.h"

#include "plugin.h"
#include "module.h"
#include "script.h"

#include "util.h"

/* the list of plugins */
static GList *plugins = NULL;

/****************/
/* dependencies */
/****************/

#define SUCCEEDS 0
#define PRECEEDS 1
#define REQUIRES 2
#define NEEDS 3
#define DEPENDS 4
#define CONFLICTS 5

const char *dependency_names[nr_dependencies] = {
  "succeeds",
  "preceeds",
  "requires",
  "needs",
  "depends",
  "conflicts",
};

/*********/
/* hooks */
/*********/

static void handle_vlock_start(const char * hook_name);
static void handle_vlock_end(const char * hook_name);
static void handle_vlock_save(const char * hook_name);
static void handle_vlock_save_abort(const char * hook_name);

const struct hook hooks[nr_hooks] = {
  { "vlock_start", handle_vlock_start },
  { "vlock_end", handle_vlock_end },
  { "vlock_save", handle_vlock_save },
  { "vlock_save_abort", handle_vlock_save_abort },
};

/**********************/
/* exported functions */
/**********************/

/* helper declarations */
static VlockPlugin *__load_plugin(const char *name, GError **error);
static bool __resolve_depedencies(GError **error);
static bool sort_plugins(GError **error);

bool load_plugin(const char *name, GError **error)
{
  return __load_plugin(name, error) != NULL;
}

bool resolve_dependencies(GError **error)
{
  return __resolve_depedencies(error) && sort_plugins(error);
}

void unload_plugins(void)
{
  while (plugins != NULL) {
    g_object_unref(plugins->data);
    plugins = g_list_delete_link(plugins, plugins);
  }
}

void plugin_hook(const char *hook_name)
{
  for (size_t i = 0; i < nr_hooks; i++)
    /* Get the handler and call it. */
    if (strcmp(hook_name, hooks[i].name) == 0) {
      hooks[i].handler(hook_name);
      break;
    }
}

/********************/
/* helper functions */
/********************/

static gint plugin_name_compare(VlockPlugin *p, const char *name)
{
  return strcmp(name, p->name);
}

static VlockPlugin *get_plugin(const char *name)
{
  GList *item = g_list_find_custom(plugins,
                                   name,
                                   (GCompareFunc) plugin_name_compare);

  if (item != NULL)
    return item->data;
  else
    return NULL;
}

/* Load and return the named plugin. */
static VlockPlugin *__load_plugin(const char *name, GError **error)
{
  VlockPlugin *p = get_plugin(name);

  if (p != NULL)
    return p;

  GError *err = NULL;

  /* Possible plugin types. */
  GType plugin_types[] = { TYPE_VLOCK_MODULE, TYPE_VLOCK_SCRIPT, 0 };

  for (size_t i = 0; plugin_types[i] != 0; i++) {
    if (err == NULL || g_error_matches(err,
                                       VLOCK_PLUGIN_ERROR,
                                       VLOCK_PLUGIN_ERROR_NOT_FOUND))
      /* Continue if the was no previous error or the error was "not found". */
      g_clear_error(&err);
    else
      /* Bail out on real errors. */
      break;

    /* Create the plugin. */
    p = g_object_new(plugin_types[i], "name", name, NULL);

    /* Try to open the plugin. */
    if (vlock_plugin_open(p, &err)) {
      g_assert(err == NULL);
      break;
    } else {
      g_assert(err != NULL);
      g_object_unref(p);
      p = NULL;
    }
  }

  if (err != NULL) {
    g_assert(p == NULL);

    g_propagate_error(error, err);

    return NULL;
  } else {
    g_assert(p != NULL);

    plugins = g_list_append(plugins, p);

    return p;
  }
}

/* Resolve the dependencies of the plugins. */
static bool __resolve_depedencies(GError **error)
{
  GList *required_plugins = NULL;

  /* Load plugins that are required.  This automagically takes care of plugins
   * that are required by the plugins loaded here because they are appended to
   * the end of the list. */
  for (GList *plugin_item = plugins;
       plugin_item != NULL;
       plugin_item = g_list_next(plugin_item)) {
    VlockPlugin *p = plugin_item->data;

    for (GList *dependency_item = p->dependencies[REQUIRES];
         dependency_item != NULL;
         dependency_item = g_list_next(dependency_item)) {
      const char *d = dependency_item->data;
      VlockPlugin *q = __load_plugin(d, NULL);

      if (q == NULL) {
        g_set_error(
          error,
          VLOCK_PLUGIN_ERROR,
          VLOCK_PLUGIN_ERROR_DEPENDENCY,
          "'%s' requires '%s' which could not be loaded", p->name, d);
        g_list_free(required_plugins);
        return false;
      }

      required_plugins = g_list_append(required_plugins, p);
    }
  }

  /* Fail if a plugins that is needed is not loaded. */
  for (GList *plugin_item = plugins;
       plugin_item != NULL;
       plugin_item = g_list_next(plugin_item)) {
    VlockPlugin *p = plugin_item->data;

    for (GList *dependency_item = p->dependencies[NEEDS];
         dependency_item != NULL;
         dependency_item = g_list_next(dependency_item)) {
      const char *d = dependency_item->data;
      VlockPlugin *q = get_plugin(d);

      if (q == NULL) {
        g_set_error(
          error,
          VLOCK_PLUGIN_ERROR,
          VLOCK_PLUGIN_ERROR_DEPENDENCY,
          "'%s' needs '%s' which is not loaded", p->name, d);
        g_list_free(required_plugins);
        errno = 0;
        return false;
      }

      required_plugins = g_list_append(required_plugins, q);
    }
  }

  /* Unload plugins whose prerequisites are not present, fail if those plugins
   * are required. */
  for (GList *plugin_item = plugins;
       plugin_item != NULL; ) {
    VlockPlugin *p = plugin_item->data;
    bool dependencies_loaded = true;

    for (GList *dependency_item = p->dependencies[DEPENDS];
         dependency_item != NULL;
         dependency_item = g_list_next(dependency_item)) {
      const char *d = dependency_item->data;
      VlockPlugin *q = get_plugin(d);

      if (q == NULL) {
        dependencies_loaded = false;

        /* Abort if dependencies not met and plugin is required. */
        if (g_list_find(required_plugins, p) != NULL) {
          g_set_error(
            error,
            VLOCK_PLUGIN_ERROR,
            VLOCK_PLUGIN_ERROR_DEPENDENCY,
            "'%s' is required by some other plugin but depends on '%s' which is not loaded",
            p->name,
            d);
          g_list_free(required_plugins);
          errno = 0;
          return false;
        }

        break;
      }
    }

    GList *next_plugin_item = g_list_next(plugin_item);

    if (!dependencies_loaded) {
      g_object_unref(p);
      plugins = g_list_delete_link(plugins, plugin_item);
    }

    plugin_item = next_plugin_item;
  }

  g_list_free(required_plugins);

  /* Fail if conflicting plugins are loaded. */
  for (GList *plugin_item = plugins;
       plugin_item != NULL;
       plugin_item = g_list_next(plugin_item)) {
    VlockPlugin *p = plugin_item->data;

    for (GList *dependency_item = p->dependencies[CONFLICTS];
         dependency_item != NULL;
         dependency_item = g_list_next(dependency_item)) {
      const char *d = dependency_item->data;
      if (get_plugin(d) != NULL) {
        g_set_error(
          error,
          VLOCK_PLUGIN_ERROR,
          VLOCK_PLUGIN_ERROR_DEPENDENCY,
          "'%s' and '%s' cannot be loaded at the same time",
          p->name,
          d);
        errno = 0;
        return false;
      }
    }
  }

  return true;
}

static GList *get_edges(void);

/* Sort the list of plugins according to their "preceeds" and "succeeds"
* dependencies.  Fails if sorting is not possible because of circles. */
static bool sort_plugins(GError **error)
{
  GList *edges = get_edges();
  GList *sorted_plugins;

  /* Topological sort. */
  sorted_plugins = tsort(plugins, &edges);

  bool tsort_successful = (edges == NULL);

  if (tsort_successful) {
    /* Switch the global list of plugins for the sorted list.  The global list
     * is static and cannot be freed. */

    g_assert(edges == NULL);
    g_assert(g_list_length(sorted_plugins) == g_list_length(plugins));

    GList *tmp = plugins;
    plugins = sorted_plugins;

    g_list_free(tmp);

    return true;
  } else {
    GString *error_message = g_string_new("circular dependencies detected:");

    while (edges != NULL) {
      struct edge *e = edges->data;
      VlockPlugin *p = e->predecessor;
      VlockPlugin *s = e->successor;

      g_string_append_printf(error_message,
                             "\n\t'%s'\tmust come before\t'%s'",
                             p->name,
                             s->name);
      g_free(e);
      edges = g_list_delete_link(edges, edges);
    }

    g_set_error(error,
                VLOCK_PLUGIN_ERROR,
                VLOCK_PLUGIN_ERROR_DEPENDENCY,
                "%s",
                error_message->str);

    g_string_free(error_message, true);
    return false;
  }
}

/* Get the edges of the plugin graph specified by each plugin's "preceeds" and
 * "succeeds" dependencies. */
static GList *get_edges(void)
{
  GList *edges = NULL;

  for (GList *plugin_item = plugins;
       plugin_item != NULL;
       plugin_item = g_list_next(plugin_item)) {
    VlockPlugin *p = plugin_item->data;
    /* p must come after these */
    for (GList *predecessor_item = p->dependencies[SUCCEEDS];
         predecessor_item != NULL;
         predecessor_item = g_list_next(predecessor_item)) {
      VlockPlugin *q = get_plugin(predecessor_item->data);

      if (q != NULL)
        edges = g_list_append(edges, make_edge(q, p));
    }

    /* p must come before these */
    for (GList *successor_item = p->dependencies[PRECEEDS];
         successor_item != NULL;
         successor_item = g_list_next(successor_item)) {
      VlockPlugin *q = get_plugin(successor_item->data);

      if (q != NULL)
        edges = g_list_append(edges, make_edge(p, q));
    }
  }

  return edges;
}

/************/
/* handlers */
/************/

/* Call the "vlock_start" hook of each plugin.  Fails if the hook of one of the
 * plugins fails.  In this case the "vlock_end" hooks of all plugins that were
 * called before are called in reverse order. */
void handle_vlock_start(const char *hook_name)
{
  for (GList *plugin_item = plugins;
       plugin_item != NULL;
       plugin_item = g_list_next(plugin_item)) {
    VlockPlugin *p = plugin_item->data;

    if (!vlock_plugin_call_hook(p, hook_name)) {
      int errsv = errno;

      for (GList *reverse_item = g_list_previous(plugin_item);
           reverse_item != NULL;
           reverse_item = g_list_previous(reverse_item)) {
        VlockPlugin *r = reverse_item->data;
        (void) vlock_plugin_call_hook(r, "vlock_end");
      }

      if (errsv)
        fprintf(stderr, "vlock: plugin '%s' failed: %s\n", p->name,
                strerror(errsv));

      exit(EXIT_FAILURE);
    }
  }
}

/* Call the "vlock_end" hook of each plugin in reverse order.  Never fails. */
void handle_vlock_end(const char *hook_name)
{
  for (GList *plugin_item = g_list_last(plugins);
       plugin_item != NULL;
       plugin_item = g_list_previous(plugin_item)) {
    VlockPlugin *p = plugin_item->data;
    (void) vlock_plugin_call_hook(p, hook_name);
  }
}

/* Call the "vlock_save" hook of each plugin.  Never fails.  If the hook of a
 * plugin fails its "vlock_save_abort" hook is called and both hooks are never
 * called again afterwards. */
void handle_vlock_save(const char *hook_name)
{
  for (GList *plugin_item = plugins;
       plugin_item != NULL;
       plugin_item = g_list_next(plugin_item)) {
    VlockPlugin *p = plugin_item->data;

    if (p->save_disabled)
      continue;

    if (!vlock_plugin_call_hook(p, hook_name)) {
      p->save_disabled = true;
      (void) vlock_plugin_call_hook(p, "vlock_save_abort");
    }
  }
}

/* Call the "vlock_save" hook of each plugin.  Never fails.  If the hook of a
 * plugin fails both hooks "vlock_save" and "vlock_save_abort" are never called
 * again afterwards. */
void handle_vlock_save_abort(const char *hook_name)
{
  for (GList *plugin_item = g_list_last(plugins);
       plugin_item != NULL;
       plugin_item = g_list_previous(plugin_item)) {
    VlockPlugin *p = plugin_item->data;

    if (p->save_disabled)
      continue;

    if (!vlock_plugin_call_hook(p, hook_name))
      p->save_disabled = true;
  }
}

