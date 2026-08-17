#pragma once

#include "plugin.h"

typedef struct VlockModule VlockModule;

/* A hook function as defined by a module. */
typedef bool (*module_hook_function)(void **);

struct VlockModule
{
  VlockPlugin parent;

  /* Handle returned by dlopen(). */
  void *dl_handle;

  /* Pointer to be used by the module's hooks. */
  void *hook_context;

  /* Array of hook functions defined by a single module.  Stored in the same
   * order as the global hooks. */
  module_hook_function hooks[nr_hooks];
};

/* Allocate and initialize a module plugin for `name`.  Returns NULL on OOM. */
VlockPlugin *vlock_module_new(const char *name);
