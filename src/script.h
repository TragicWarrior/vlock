#pragma once

#include <glib-object.h>

/*
 * Script type macros.
 */
#define TYPE_VLOCK_SCRIPT (vlock_script_get_type())
#define VLOCK_SCRIPT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_VLOCK_SCRIPT,\
                                                      VlockScript))
#define VLOCK_SCRIPT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),\
                                                           TYPE_VLOCK_SCRIPT,\
                                                           VlockScriptClass))
#define IS_VLOCK_SCRIPT(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                                         TYPE_VLOCK_SCRIPT))
#define IS_VLOCK_SCRIPT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                                              TYPE_VLOCK_SCRIPT))
#define VLOCK_SCRIPT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),\
                                                               TYPE_VLOCK_SCRIPT,\
                                                               VlockScriptClass))

typedef struct _VlockScript VlockScript;
typedef struct _VlockScriptClass VlockScriptClass;

typedef struct _VlockScriptPrivate VlockScriptPrivate;

struct _VlockScript
{
  VlockPlugin parent_instance;

  VlockScriptPrivate *priv;
};

struct _VlockScriptClass
{
  VlockPluginClass parent_class;
};

GType vlock_script_get_type(void);
