#ifndef UCA_PHANTOM_VARIABLES_H
#define UCA_PHANTOM_VARIABLES_H

#include "uca-phantom-communicate.h"

#include <glib-object.h>

typedef struct {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    gint         property_id;
    const gchar *description;
} PhantomUnit;

extern PhantomUnit variables[];

#endif