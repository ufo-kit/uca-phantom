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
        print_gvalue (variable_flag, &val);
    }

    g_value_unset (&val);
}

gboolean main() {
    GError *error = NULL;
    gboolean result;

    int cine = 1;
    guint nb_images = 150;

    UcaPhantomCommunicate *communicator = g_object_new (UCA_TYPE_PHANTOM_COMMUNICATE, 
        "phantom_ipsource", USE_CLASS,
        "xenabled", TRUE,
        "xnetcard", "enp5s0f1",
        "timestamping", FALSE,
        NULL);
    
    g_message ("Created communicator\n");
   
    gboolean connected = uca_phantom_communicate_connect_controlstream(communicator, &error);
    if (!connected && error != NULL) {
        g_print ("Houston theres a problem: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    result = uca_phantom_communicate_connect_xdatastream (communicator, &error);
    if (!result && error != NULL) {
        g_print ("Houston theres a problem: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    g_message ("Connection done\n");
    
    result = uca_phantom_communicate_start_readout(communicator, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    result = uca_phantom_communicate_arm (communicator, cine, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    sleep(3);


    result = uca_phantom_communicate_trigger (communicator, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    clock_t start = clock() ;
    // Grab frames from phantom cine
    // result = uca_phantom_communicate_request_images(communicator, cine, nb_images, IMG_8, TS_SHORT, &error);
    // if (!result && error != NULL) {
    //     g_print ("Yo there was an error: %s\n", error->message);
    //     g_error_free (error);
    //     g_object_unref (communicator);
    //     return FALSE;
    // }

    // pause for 1 second

    result = uca_phantom_communicate_request_images(communicator, cine, nb_images, IMG_P10, TS_NONE, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error: %s\n", error->message);
        g_error_free (error);
        g_object_unref (communicator);
        return FALSE;
    }

    // allocate memory for image
    guint16 *image = g_malloc0 (2 * 2048 * 1952);
    gchar *filename = NULL;

    for (guint i = 0; i < nb_images; i++) {
        g_print ("Grabbing image %d\n", i);
        // get image
        result = uca_phantom_communicate_grab_image (communicator, image, &error);
        if (!result && error != NULL) {
            g_print ("Yo there was an error when trying to grab the image: %s\n", error->message);
            g_error_free (error);
            g_object_unref (communicator);
            return FALSE;
        }

        filename = g_strdup_printf("test_image_%d.raw", i);

        // Save image to file
        save_image(image, 2048, 1952, filename);

        g_print ("Saved image %d to %s\n", i, filename);
        g_free (filename);
    }
    g_print ("Stopping\n");
    result = uca_phantom_communicate_stop_readout(communicator, &error);
    if (!result && error != NULL) {
        g_print ("Yo there was an error whenn trying to stop: %s\n", error->message);
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