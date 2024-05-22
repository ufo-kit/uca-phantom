#include "utilities.h"
#include <glib.h>

// Function that saves a 16-bit image to a file
gboolean save_image_8 (guint8 *image, guint width, guint height, const gchar *filename) {
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        g_print ("Error opening file: %s\n", filename);
        return FALSE;
    }

    // Write image data
    gssize nb_bytes = fwrite(image, sizeof(guint8), width * height, fp);

    if (nb_bytes != width * height) {
        g_print ("Error writing file: %s. Only wrote %ld of %d\n", filename, nb_bytes, width * height * 2);
        return FALSE;
    }

    // Close file
    fclose(fp);

    return TRUE;
}

// Function that saves a 16-bit image to a file
gboolean save_image_16 (guint16 *image, guint width, guint height, const gchar *filename) {
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