/* script.c -- script routines for vlock, the VT locking program for linux
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

/* Scripts are executables that are run as unprivileged child processes of
 * vlock.  They communicate with vlock through stdin and stdout.
 *
 * When dependencies are retrieved they are launched once for each dependency
 * and should print the names of the plugins they depend on on stdout one per
 * line.  The dependency requested is given as a single command line argument.
 *
 * In hook mode the script is called once with "hooks" as a single command line
 * argument.  It should not exit until its stdin closes.  The hook that should
 * be executed is written to its stdin on a single line.
 *
 * Currently there is no way for a script to communicate errors or even success
 * to vlock.  If it exits it will linger as a zombie until the plugin is
 * destroyed.
 */

#if !defined(__FreeBSD__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include <glib.h>
#include <glib-object.h>

#include "process.h"
#include "util.h"

#include "plugin.h"
#include "script.h"

static char *read_dependency(const char *path,
                             const char *dependency_name,
                             GError **error);
static void parse_dependency(char *data, GList **dependency_list);

/* Get the dependency from the script. */
static bool get_dependency(const char *path, const char *dependency_name,
                           GList **dependency_list, GError **error)
{
  GError *tmp_error = NULL;

  /* Read the dependency data. */
  char *data = read_dependency(path, dependency_name, &tmp_error);

  if (data == NULL) {
    if (tmp_error != NULL) {
      g_propagate_error(error, tmp_error);
      return false;
    } else
      /* No data. */
      return true;
  }

  /* Parse the dependency data into the list. */
  parse_dependency(data, dependency_list);

  g_free(data);

  return true;
}

/* Read the dependency data by starting the script with the name of the
 * dependency as a single command line argument.  The script should then print
 * the dependencies to its stdout one on per line. */
static char *read_dependency(const char *path,
                             const char *dependency_name,
                             GError **error)
{
  GError *tmp_error = NULL;
  const char *argv[] = { path, dependency_name, NULL };
  struct child_process child = {
    .path = path,
    .argv = argv,
    .stdin_fd = REDIRECT_DEV_NULL,
    .stdout_fd = REDIRECT_PIPE,
    .stderr_fd = REDIRECT_DEV_NULL,
    .function = NULL,
  };
  /* Timeout is one second. */
  struct timeval timeout = {1, 0};
  char *data = g_malloc(sizeof *data);
  size_t data_length = 0;

  if (!create_child(&child, &tmp_error)) {
    g_assert(tmp_error != NULL);
    g_propagate_error(error, tmp_error);
    return NULL;
  }

  /* Read the dependency from the child.  Reading fails if either the timeout
   * elapses or more that LINE_MAX bytes are read. */
  for (;;) {
    struct timeval t = timeout;
    struct timeval t1;
    struct timeval t2;
    char buffer[LINE_MAX];
    ssize_t length;

    fd_set read_fds;

    FD_ZERO(&read_fds);
    FD_SET(child.stdout_fd, &read_fds);

    /* t1 is before select. */
    (void) gettimeofday(&t1, NULL);

    if (select(child.stdout_fd+1, &read_fds, NULL, NULL, &t) != 1) {
timeout:
      g_set_error(&tmp_error,
                  VLOCK_PLUGIN_ERROR,
                  VLOCK_PLUGIN_ERROR_FAILED,
                  "reading dependency (%s) data from script %s failed: timeout",
                  dependency_name,
                  /* XXX: plugin->name */ path
                  );
      goto error;
    }

    /* t2 is after select. */
    (void) gettimeofday(&t2, NULL);

    /* Get the time that during select. */
    timersub(&t2, &t1, &t2);

    /* This is very unlikely. */
    if (timercmp(&t2, &timeout, >))
      goto timeout;

    /* Reduce the timeout. */
    timersub(&timeout, &t2, &timeout);

    /* Read dependency data from the script. */
    length = read(child.stdout_fd, buffer, sizeof buffer - 1);

    /* Did the script close its stdin or exit? */
    if (length <= 0)
      break;

    if (data_length+length+1 > LINE_MAX) {
      g_set_error(
        &tmp_error,
        VLOCK_PLUGIN_ERROR,
        VLOCK_PLUGIN_ERROR_FAILED,
        "reading dependency (%s) data from script %s failed: too much data",
        dependency_name,
        /* XXX: plugin->name */ path
        );
      goto error;
    }

    /* Grow the data string. */
    data = g_realloc(data, data_length+length);

    /* Append the buffer to the data string. */
    strncpy(data+data_length, buffer, length);
    data_length += length;
  }

  /* Terminate the data string. */
  data[data_length] = '\0';

error:
  /* Close the read end of the pipe. */
  (void) close(child.stdout_fd);
  /* Kill the script. */
  if (!wait_for_death(child.pid, 0, 500000L))
    ensure_death(child.pid);

  if (tmp_error != NULL) {
    g_propagate_error(error, tmp_error);
    g_free(data);
    data = NULL;
  }

  return data;
}

static void parse_dependency(char *data, GList **dependency_list)
{
  char **dependency_items = g_strsplit_set(g_strstrip(data), " \r\n", -1);

  for (size_t i = 0; dependency_items[i] != NULL; i++)
    *dependency_list = g_list_append(
      *dependency_list,
      g_strdup(dependency_items[i])
      );

  g_strfreev(dependency_items);
}

G_DEFINE_TYPE(VlockScript, vlock_script, TYPE_VLOCK_PLUGIN)

#define VLOCK_SCRIPT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj),\
                                                                   TYPE_VLOCK_SCRIPT,\
                                                                   VlockScriptPrivate))

struct _VlockScriptPrivate
{
  /* The path to the script. */
  char *path;
  /* Was the script launched? */
  bool launched;
  /* Did the script die? */
  bool dead;
  /* The pipe file descriptor that is connected to the script's stdin. */
  int fd;
  /* The PID of the script. */
  pid_t pid;
};

/* Initialize plugin to default values. */
static void vlock_script_init(VlockScript *self)
{
  self->priv = VLOCK_SCRIPT_GET_PRIVATE(self);

  self->priv->dead = false;
  self->priv->launched = false;
  self->priv->path = NULL;
}

static void vlock_script_finalize(GObject *object)
{
  VlockScript *self = VLOCK_SCRIPT(object);

  g_free(self->priv->path);

  if (self->priv->launched) {
    /* Close the pipe. */
    (void) close(self->priv->fd);

    /* Kill the child process. */
    if (!wait_for_death(self->priv->pid, 0, 500000L))
      ensure_death(self->priv->pid);
  }

  G_OBJECT_CLASS(vlock_script_parent_class)->finalize(object);
}

static bool vlock_script_open(VlockPlugin *plugin, GError **error)
{
  GError *tmp_error = NULL;
  VlockScript *self = VLOCK_SCRIPT(plugin);

  self->priv->path = g_strdup_printf("%s/%s", VLOCK_SCRIPT_DIR, plugin->name);

  /* Get the dependency information.  Whether the script is executable or not
   * is also detected here. */
  for (size_t i = 0; i < nr_dependencies; i++)
    if (!get_dependency(self->priv->path, dependency_names[i],
                        &plugin->dependencies[i], &tmp_error)) {
      if (g_error_matches(tmp_error,
                          VLOCK_PROCESS_ERROR,
                          VLOCK_PROCESS_ERROR_NOT_FOUND) && i == 0) {
        g_set_error(error, VLOCK_PLUGIN_ERROR, VLOCK_PLUGIN_ERROR_NOT_FOUND,
                    "%s", tmp_error->message);
        g_clear_error(&tmp_error);
      } else
        g_propagate_error(error, tmp_error);

      return false;
    }

  return true;
}

/* Launch the script creating a new script_context. */
static bool vlock_script_launch(VlockScript *script, GError **error)
{
  GError *tmp_error = NULL;
  int fd_flags;
  const char *argv[] = { script->priv->path, "hooks", NULL };
  struct child_process child = {
    .path = script->priv->path,
    .argv = argv,
    .stdin_fd = REDIRECT_PIPE,
    .stdout_fd = REDIRECT_DEV_NULL,
    .stderr_fd = REDIRECT_DEV_NULL,
    .function = NULL,
  };

  if (!create_child(&child, &tmp_error)) {
    g_propagate_error(error, tmp_error);
    return false;
  }

  script->priv->fd = child.stdin_fd;
  script->priv->pid = child.pid;

  fd_flags = fcntl(script->priv->fd, F_GETFL, &fd_flags);

  if (fd_flags != -1) {
    fd_flags |= O_NONBLOCK;
    (void) fcntl(script->priv->fd, F_SETFL, fd_flags);
  }

  return true;
}

static bool vlock_script_call_hook(VlockPlugin *plugin, const gchar *hook_name)
{
  VlockScript *self = VLOCK_SCRIPT(plugin);
  static const char newline = '\n';
  ssize_t hook_name_length = strlen(hook_name);
  ssize_t length;
  struct sigaction act;
  struct sigaction oldact;

  if (!self->priv->launched) {
    /* Launch script. */
    self->priv->launched = vlock_script_launch(self, NULL);

    if (!self->priv->launched) {
      /* Do not retry. */
      self->priv->dead = true;
      return false;
    }
  }

  if (self->priv->dead)
    /* Nothing to do. */
    return false;

  /* When writing to a pipe when the read end is closed the kernel invariably
   * sends SIGPIPE.   Ignore it. */
  (void) sigemptyset(&(act.sa_mask));
  act.sa_flags = SA_RESTART;
  act.sa_handler = SIG_IGN;
  (void) sigaction(SIGPIPE, &act, &oldact);

  /* Send hook name and a newline through the pipe. */
  length = write(self->priv->fd, hook_name, hook_name_length);

  if (length > 0)
    length += write(self->priv->fd, &newline, sizeof newline);

  /* Restore the previous SIGPIPE handler. */
  (void) sigaction(SIGPIPE, &oldact, NULL);

  /* If write fails the script is considered dead. */
  self->priv->dead = (length != hook_name_length + 1);

  return !self->priv->dead;
}

/* Initialize script class. */
static void vlock_script_class_init(VlockScriptClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  VlockPluginClass *plugin_class = VLOCK_PLUGIN_CLASS(klass);

  g_type_class_add_private(klass, sizeof(VlockScriptPrivate));

  /* Virtual methods. */
  gobject_class->finalize = vlock_script_finalize;

  plugin_class->open = vlock_script_open;
  plugin_class->call_hook = vlock_script_call_hook;
}

