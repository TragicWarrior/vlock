#pragma once

#include <sys/types.h>
#include "plugin.h"

typedef struct VlockScript VlockScript;

struct VlockScript
{
  VlockPlugin parent;

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

/* Allocate and initialize a script plugin for `name`.  Returns NULL on OOM. */
VlockPlugin *vlock_script_new(const char *name);
