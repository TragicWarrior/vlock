/* plugins.h -- plugins header file for vlock,
 *              the VT locking program for linux
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

/* Load the named plugin. */
bool load_plugin(const char *name, GError **error);

/* Resolve all the dependencies between all plugins.  This function *must* be
 * called after all plugins were loaded.  */
bool resolve_dependencies(GError **error);

/* Unload all plugins. */
void unload_plugins(void);

/* Call the given plugin hook. */
void plugin_hook(const char *hook_name);
