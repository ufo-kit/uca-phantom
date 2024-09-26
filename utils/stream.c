#include <uca/uca-plugin-manager.h>
#include <uca/uca-camera.h>
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>

gboolean running = TRUE;

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

void signal_handler (void) {
    running = FALSE;
}


int main (int argc, char *argv[]) {
    // Declaring the variables to be used
    UcaPluginManager *manager;
    UcaCamera *camera;
    GError *error = NULL;

    // Creating the camera object
    manager = uca_plugin_manager_new();

    camera = uca_plugin_manager_get_camera(manager, 
        "phantom", &error, NULL);    
    if (error != NULL) {
        g_print("Error: %s\n", error->message);
        return 1;
    }

    int preframes = 0;
    int postframes = 500;
    int nb_triggers = 1;
    int res_x = 2048, res_y = 1952;

    // Connecting to the camera and starting the readout threads
    g_print ("Setting properties\n");
    g_object_set(
        G_OBJECT(camera),
        "frames-per-second", 100, 
        "xenabled", TRUE,
        "xnetcard", "enp5s0f1",
        "tsformat", 4,
        "roi-width", res_x,
        "roi-height", res_y,
        "liveimages", TRUE,
        NULL);
    
    g_print ("Calling readout\n");

    uca_camera_start_readout (camera, &error);
    if (error != NULL) {
        g_print("Error: %s\n", error->message);
        return 1;
    }

    uca_camera_start_recording(camera, &error);
    if (error != NULL) {
        g_print("Error: %s\n", error->message);
        return 1;
    }
    
    while (TRUE) {
        // Allocate memory for the image
        guint16 *image = g_malloc(res_x * res_y * sizeof(guint16));

        uca_camera_grab (camera, image, &error);
        if (error != NULL) {
            g_print("Error: %s\n", error->message);
            return 1;
        }

        // Do stuff with the image

        // free
        g_free(image);
    }

    // Cleaning up!
    uca_camera_stop_recording(camera, &error);

    uca_camera_stop_readout(camera, &error);
    
    g_object_unref(camera);
    g_object_unref(manager);
}