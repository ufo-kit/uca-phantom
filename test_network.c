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
    gssize nb_bytes = fwrite(image, sizeof(guint16), width * height, fp);

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
        print_gvalue (variable_flag, &val);
    }

    g_value_unset (&val);
}

gboolean main() {
    GError *error = NULL;
    gboolean result;

    UcaPhantomCommunicate *communicator = g_object_new (UCA_TYPE_PHANTOM_COMMUNICATE, 
        "phantom_ipsource", USE_CLASS,
        "xnetcard", "enp4s0f1",
        NULL);
   
    gboolean connected = uca_phantom_communicate_attempt_connect(communicator, &error);
    if (!connected && error != NULL) {
        g_print ("Houston theres a problem: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }
    
    result = uca_phantom_communicate_start_readout(communicator, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }
   
    // Arm phantom
    result = uca_phantom_communicate_arm (communicator, "1", &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    // Trigger phantom
    result = uca_phantom_communicate_trigger (communicator, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    // // Grab frames from phantom cine
    // result = uca_phantom_communicate_request_images(communicator, 1, 2, IMG_8, TS_NONE, &error);
    // if (!result && error != NULL) {
    //     g_print ("Yo there was an error: %s\n", error->message);
    //     g_error_free (error);
    //     g_object_unref (communicator);
    //     return FALSE;
    // }

    clock_t start = clock() ;
    result = uca_phantom_communicate_request_ximages(communicator, 1, 1, IMG_P10, TS_NONE, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    // allocate memory for image
    guint16 *image = g_malloc0 (sizeof(guint16) * 2048 * 1952);
    
    // get image
    result = uca_phantom_communicate_grab_image (communicator, image, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    // Save image to file
    save_image(image, 2048, 1952, "test_image.raw");

    result = uca_phantom_communicate_stop_readout(communicator, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }
    double elapsed_time = (clock()-start)/(double)CLOCKS_PER_SEC ;
    g_print ("Elapsed time: %f\n", elapsed_time);

    g_free (image);

    g_object_unref (communicator);
    return TRUE;
}