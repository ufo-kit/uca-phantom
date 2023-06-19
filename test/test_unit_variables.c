#include <glib-object.h>
#include <stdio.h>
#include <time.h>

#include "uca-phantom-communicate.h"

// Function that saves a 16-bit image to a file
gboolean save_image(guint16 *image, guint width, guint height, const gchar *filename) {
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        g_print ("Error opening file: %s\n", filename);
        return FALSE;
    }

    // Write image data
    gssize nb_bytes = fwrite(image, 2, width * height, fp);

    if (nb_bytes != width * height) {
        g_print ("Error writing file: %s. Only wrote %ld of %d\n", filename, nb_bytes, width * height * 2);
        return FALSE;
    }

    // Close file
    fclose(fp);

    return TRUE;
}

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

    result = uca_phantom_communicate_get_variable (communicator, variable_flag, &val, &error);

    if (!result && error != NULL) {
        g_print ("Houston theres a problem: %s", error->message);
    }
    else {
        // print_gvalue (variable_flag, &val);
    }

    g_value_unset (&val);
}

// * Message: 14:55:57.877: > request:
// get info.rtopacket
 

// ** Message: 14:55:57.877: > reply:
// rtopacket : 160
 

// ** Message: 14:55:57.878: > request:
// get info.rtopacketovhead
 

// ** Message: 14:55:57.878: > reply:
// rtopacketovhead : 16
 

// ** Message: 14:55:57.878: > request:
// get info.rtofrovhead
 

// ** Message: 14:55:57.878: > reply:
// rtofrovhead : 6000
 

// ** Message: 14:55:57.878: > request:
// get info.rto_channels
 

// ** Message: 14:55:57.878: > reply:
// rto_channels : 8


gboolean main() {
    GError *error = NULL;
    gboolean result;

    UcaPhantomCommunicate *communicator = g_object_new (UCA_TYPE_PHANTOM_COMMUNICATE, 
        "phantom_ipsource", USE_CLASS,
        "xenabled", TRUE,
        "xnetcard", "enp5s0f1",
        "timestamping", FALSE,
        NULL);
   
    gboolean connected = uca_phantom_communicate_connect_controlstream(communicator, &error);
    if (!connected && error != NULL) {
        g_print ("Houston theres a problem: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    // attempt_get_variable (communicator, UNIT_INFO_CINEMEM);
    // attempt_get_variable (communicator, UNIT_INFO_MEMSZ);


    // attempt_get_variable (communicator, UNIT_CT_FRCOUNT);
    // attempt_get_variable (communicator, UNIT_CT_STATE);
    // attempt_get_variable (communicator, UNIT_CT_FIRSTFR);
    // attempt_get_variable (communicator, UNIT_CT_LASTFR);
    // attempt_get_variable (communicator, UNIT_CT_IN);
    // attempt_get_variable (communicator, UNIT_CT_OUT);

    // attempt_get_variable (communicator, UNIT_INFO_RTOPACKETOVHEAD);
    // attempt_get_variable (communicator, UNIT_INFO_RTOFROVHEAD);
    // attempt_get_variable (communicator, UNIT_INFO_RTO_CHANNELS);

    // attempt_get_variable (communicator, UNIT_INFO_MODES);
    // attempt_get_variable (communicator, UNIT_CAM_MODE);
    // attempt_get_variable (communicator, UNIT_CAM_SYNCIMG);
    // attempt_get_variable (communicator, UNIT_CAM_RTOEN);
    // attempt_get_variable (communicator, UNIT_CAM_STARTONACQ);
    // attempt_get_variable (communicator, UNIT_INFO_FEATURES);

    // attempt_get_variable (communicator, UNIT_DEFC_RATE);
    // attempt_get_variable (communicator, UNIT_DEFC_RES);


    g_object_unref (communicator);
    return TRUE;
}