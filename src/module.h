#pragma once

#include <glib-object.h>
#include "plugin.h"

/*
 * Module type macros.
 */
#define TYPE_VLOCK_MODULE (vlock_module_get_type())
#define VLOCK_MODULE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_VLOCK_MODULE,\
                                                      VlockModule))
#define VLOCK_MODULE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),\
                                                           TYPE_VLOCK_MODULE,\
                                                           VlockModuleClass))
#define IS_VLOCK_MODULE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                                         TYPE_VLOCK_MODULE))
#define IS_VLOCK_MODULE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                                              TYPE_VLOCK_MODULE))
#define VLOCK_MODULE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),\
                                                               TYPE_VLOCK_MODULE,\
                                                               VlockModuleClass))

typedef struct _VlockModule VlockModule;
typedef struct _VlockModuleClass VlockModuleClass;

typedef struct _VlockModulePrivate VlockModulePrivate;

struct _VlockModule
{
  VlockPlugin parent_instance;

  VlockModulePrivate *priv;
};

struct _VlockModuleClass
{
  VlockPluginClass parent_class;
};

GType vlock_module_get_type(void);
