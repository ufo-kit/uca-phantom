#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>
#include <time.h>

#include "uca-phantom-communicate.h"

void print_gvalue(guint i, const GValue *value) {
    GType type = G_VALUE_TYPE(value);

    if (type == G_TYPE_INT) {
        g_print ("Property %d: %d\n", i, g_value_get_int(value));
    }
    else if (type == G_TYPE_DOUBLE) {
        g_print ("Property %d: %f\n", i, g_value_get_double(value));
    }
    else if (type == G_TYPE_FLOAT) {
        g_print ("Property %d: %f\n", i, g_value_get_float(value));
    }
    else if (type == G_TYPE_STRING) {
        g_print ("Property %d: %s\n", i, g_value_get_string(value));
    }
    else if (type == G_TYPE_UINT) {
        g_print ("Property %d: %u\n", i, g_value_get_uint(value));
    }
    else {
        g_print ("GValue (unknown type): %s\n", g_type_name(type));
    }
}

void attempt_get_variable(UcaPhantomCommunicate *communicator, guint variable_flag) {
    GError *error = NULL;
    GValue val = G_VALUE_INIT;
    gboolean result = FALSE;

    result = uca_phantom_get_variable (communicator, variable_flag, &val, &error);

    if (!result && error != NULL) {
        g_print ("Houston theres a problem: %s", error->message);
    }
    else {
        print_gvalue (variable_flag, &val);
    }

    g_value_unset (&val);
}

gboolean main() {
    GError *error = NULL;

    UcaPhantomCommunicate *communicator = uca_phantom_communicate_new();
    gboolean connected = uca_phantom_communicate_attempt_connect(communicator, &error);


    if (!connected) {
        g_print ("Houston theres a problem: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    if (error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
    }

    // 23214

    attempt_get_variable(communicator, PROP_META_NAME);

    uca_phantom_set_variable(communicator, PROP_META_NAME, "\"23214\"", &error);

    if (error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
    }

    attempt_get_variable(communicator, PROP_META_NAME);

    // struct timespec req, rem;
    // req.tv_sec = 0;
    // req.tv_nsec = 5e+6;

    // for (int i=0; i < N_UNIT_PROPERTIES; i++) {
    //     result = uca_phantom_get_variable (communicator, i, &val, &error);
    //     if (!result && error != NULL) {
    //         g_print ("Houston theres a problem: %s\n", error->message);
    //         g_error_free (error);
    //         g_object_unref (communicator);
    //         return FALSE;
    //     }
        
    //     // print_gvalue (i, &val);
    //     g_value_unset (&val);
    //     // nanosleep(&req, &rem);
    // }

    g_object_unref (communicator);


    return TRUE;
}