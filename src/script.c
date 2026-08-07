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
#include <ctype.h>
#include <assert.h>

#include "process.h"
#include "util.h"
#include "plugin.h"
#include "script.h"

static char *read_dependency(const char *path,
                             const char *dependency_name,
                             VError **error);
static void parse_dependency(char *data, VList **dependency_list);

/* Get the dependency from the script. */
static bool get_dependency(const char *path, const char *dependency_name,
                           VList **dependency_list, VError **error)
{
  VError *tmp_error = NULL;

  /* Read the dependency data. */
  char *data = read_dependency(path, dependency_name, &tmp_error);

  if (data == NULL) {
    if (tmp_error != NULL) {
      verror_propagate(error, tmp_error);
      return false;
    } else
      /* No data. */
      return true;
  }

  /* Parse the dependency data into the list. */
  parse_dependency(data, dependency_list);

  free(data);

  return true;
}

/* Read the dependency data by starting the script with the name of the
 * dependency as a single command line argument.  The script should then print
 * the dependencies to its stdout one on per line. */
static char *read_dependency(const char *path,
                             const char *dependency_name,
                             VError **error)
{
  VError *tmp_error = NULL;
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
  char *data = malloc(1);
  size_t data_length = 0;

  if (data == NULL) {
    verror_set(error, VLOCK_PLUGIN_ERROR, VLOCK_PLUGIN_ERROR_FAILED,
               "out of memory");
    return NULL;
  }

  if (!create_child(&child, &tmp_error)) {
    assert(tmp_error != NULL);
    verror_propagate(error, tmp_error);
    free(data);
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
      verror_set(&tmp_error,
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
      verror_set(
        &tmp_error,
        VLOCK_PLUGIN_ERROR,
        VLOCK_PLUGIN_ERROR_FAILED,
        "reading dependency (%s) data from script %s failed: too much data",
        dependency_name,
        /* XXX: plugin->name */ path
        );
      goto error;
    }

    /* Grow the data string.  Reserve one extra byte for the terminating NUL
     * written after the loop, otherwise data[data_length] overflows by one. */
    {
      char *grown = realloc(data, data_length+length+1);
      if (grown == NULL) {
        verror_set(&tmp_error, VLOCK_PLUGIN_ERROR, VLOCK_PLUGIN_ERROR_FAILED,
                   "out of memory");
        goto error;
      }
      data = grown;
    }

    /* Append the buffer to the data string. */
    memcpy(data+data_length, buffer, length);
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
    verror_propagate(error, tmp_error);
    free(data);
    data = NULL;
  }

  return data;
}

/* Strip leading/trailing whitespace in place; return pointer into same buffer. */
static char *strstrip(char *s)
{
  char *end;

  while (*s != '\0' && isspace((unsigned char)*s))
    s++;

  if (*s == '\0')
    return s;

  end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end))
    end--;
  end[1] = '\0';
  return s;
}

static void parse_dependency(char *data, VList **dependency_list)
{
  char *p = strstrip(data);

  while (*p != '\0') {
    /* Skip separators. */
    while (*p != '\0' && (isspace((unsigned char)*p) || *p == '\r'))
      p++;
    if (*p == '\0')
      break;

    char *start = p;
    while (*p != '\0' && !isspace((unsigned char)*p) && *p != '\r')
      p++;

    if (*p != '\0') {
      *p = '\0';
      p++;
    }

    if (*start != '\0') {
      char *copy = strdup(start);
      if (copy != NULL)
        *dependency_list = vlist_append(*dependency_list, copy);
    }
  }
}

static void vlock_script_destroy(VlockPlugin *plugin)
{
  VlockScript *self = (VlockScript *)plugin;

  free(self->path);

  if (self->launched) {
    /* Close the pipe. */
    (void) close(self->fd);

    /* Kill the child process. */
    if (!wait_for_death(self->pid, 0, 500000L))
      ensure_death(self->pid);
  }

  free(self);
}

static bool vlock_script_open(VlockPlugin *plugin, VError **error)
{
  VError *tmp_error = NULL;
  VlockScript *self = (VlockScript *)plugin;

  if (asprintf(&self->path, "%s/%s", VLOCK_SCRIPT_DIR, plugin->name) < 0) {
    verror_set(error, VLOCK_PLUGIN_ERROR, VLOCK_PLUGIN_ERROR_FAILED,
               "out of memory");
    return false;
  }

  /* Get the dependency information.  Whether the script is executable or not
   * is also detected here. */
  for (size_t i = 0; i < nr_dependencies; i++)
    if (!get_dependency(self->path, dependency_names[i],
                        &plugin->dependencies[i], &tmp_error)) {
      if (verror_matches(tmp_error,
                         VLOCK_PROCESS_ERROR,
                         VLOCK_PROCESS_ERROR_NOT_FOUND) && i == 0) {
        verror_set(error, VLOCK_PLUGIN_ERROR, VLOCK_PLUGIN_ERROR_NOT_FOUND,
                   "%s", tmp_error->message);
        verror_clear(&tmp_error);
      } else
        verror_propagate(error, tmp_error);

      return false;
    }

  return true;
}

/* Launch the script creating a new script_context. */
static bool vlock_script_launch(VlockScript *script, VError **error)
{
  VError *tmp_error = NULL;
  int fd_flags;
  const char *argv[] = { script->path, "hooks", NULL };
  struct child_process child = {
    .path = script->path,
    .argv = argv,
    .stdin_fd = REDIRECT_PIPE,
    .stdout_fd = REDIRECT_DEV_NULL,
    .stderr_fd = REDIRECT_DEV_NULL,
    .function = NULL,
  };

  if (!create_child(&child, &tmp_error)) {
    verror_propagate(error, tmp_error);
    return false;
  }

  script->fd = child.stdin_fd;
  script->pid = child.pid;

  fd_flags = fcntl(script->fd, F_GETFL, &fd_flags);

  if (fd_flags != -1) {
    fd_flags |= O_NONBLOCK;
    (void) fcntl(script->fd, F_SETFL, fd_flags);
  }

  return true;
}

static bool vlock_script_call_hook(VlockPlugin *plugin, const char *hook_name)
{
  VlockScript *self = (VlockScript *)plugin;
  static const char newline = '\n';
  ssize_t hook_name_length = strlen(hook_name);
  ssize_t length;
  struct sigaction act;
  struct sigaction oldact;

  if (!self->launched) {
    /* Launch script. */
    self->launched = vlock_script_launch(self, NULL);

    if (!self->launched) {
      /* Do not retry. */
      self->dead = true;
      return false;
    }
  }

  if (self->dead)
    /* Nothing to do. */
    return false;

  /* When writing to a pipe when the read end is closed the kernel invariably
   * sends SIGPIPE.   Ignore it. */
  (void) sigemptyset(&(act.sa_mask));
  act.sa_flags = SA_RESTART;
  act.sa_handler = SIG_IGN;
  (void) sigaction(SIGPIPE, &act, &oldact);

  /* Send hook name and a newline through the pipe. */
  length = write(self->fd, hook_name, hook_name_length);

  if (length > 0)
    length += write(self->fd, &newline, sizeof newline);

  /* Restore the previous SIGPIPE handler. */
  (void) sigaction(SIGPIPE, &oldact, NULL);

  /* If write fails the script is considered dead. */
  self->dead = (length != hook_name_length + 1);

  return !self->dead;
}

static const VlockPluginClass vlock_script_class = {
  .destroy = vlock_script_destroy,
  .open = vlock_script_open,
  .call_hook = vlock_script_call_hook,
};

VlockPlugin *vlock_script_new(const char *name)
{
  VlockScript *self = calloc(1, sizeof *self);

  if (self == NULL)
    return NULL;

  if (!vlock_plugin_init(&self->parent, &vlock_script_class, name)) {
    free(self);
    return NULL;
  }

  self->dead = false;
  self->launched = false;
  self->path = NULL;
  return &self->parent;
}
