#ifndef UCA_PHANTOM_VARIABLES_H
#define UCA_PHANTOM_VARIABLES_H

#include "uca-phantom-communicate.h"

#include <glib-object.h>

#define VERBOSE "verbose"
#define PERFORMANCE "performance"
#define DEBUG "debug"

typedef struct {
    const gchar *name;
    GType        type;
    GParamFlags  flags;
    UcaUnit      unit;
    gint         property_id;
    const gchar *description;
} PhantomUnit;

extern PhantomUnit variables[];

#endif