#include "utilities.h"
#include <uca/uca-plugin-manager.h>
#include <uca/uca-camera.h>
#include <stdio.h>
#include <glib.h>

const gchar *filename = "/dev/null"; // Change this to the path where you want to save the images
guint expected_images = 0, res_x = 0, res_y = 0;

gpointer image_grabber (gpointer data) {
    GError *error = NULL;
    UcaCamera *camera = data;
    int nb_images = 0;
    guint8 *buffer = g_malloc0(sizeof(guint8) * res_x * res_y);
    while (nb_images < expected_images) {
        uca_camera_grab(camera, buffer, &error);
        if (error != NULL) {
            g_print("Error: %s\n", error->message);
            return NULL;
        }
        save_image_8 (buffer, res_x, res_y, filename);
        nb_images++;
    }

    g_free(buffer); 

    g_print ("Grabbed %d images\n", nb_images);

    return NULL;
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

    int preframes = 1;
    int postframes = 0;
    guint nb_recordings = 20;

    expected_images = nb_recordings * (preframes + postframes);

    // Get the max resolution of the camera
    g_object_get(
        G_OBJECT(camera),
        "info-xmax", &res_x,
        "info-ymax", &res_y,
        NULL);

    // Connecting to the camera and starting the readout threads
    g_print ("Setting properties\n");
    g_object_set(
        G_OBJECT(camera),
        "frames-per-second", (float)1000,
        "xenabled", FALSE, // Images over 1Gb connection
        "imgformat", 0, // Native 12 bit, to 16-bit
        "trigger-source", UCA_CAMERA_TRIGGER_SOURCE_AUTO,
        "syncmode", 0, // Sync exposure time and trigger
        // "exposure-time", (gdouble)(1001), // nano seconds
        // "shutteroff", TRUE,
        "preframes", preframes,
        "postframes", postframes,
        NULL);

    uca_camera_start_recording(camera, &error);
    if (error != NULL) {
        g_print("Error: %s\n", error->message);
        return 1;
    }

    // Start the image grabber thread
    GThread *thread = g_thread_new("image_grabber", (GThreadFunc) image_grabber, (gpointer)camera);

    // Trigger the cines
    for (int i=0; i < nb_recordings; i++) { // Number of recordings
        // Wait for input
        uca_camera_trigger (camera, &error);
        if (error != NULL) {
            g_print("Error: %s\n", error->message);
            return 1;
        }
        getchar();
    }

    g_thread_join (thread);

    // Cleaning up!
    uca_camera_stop_recording(camera, &error);
    
    g_object_unref(camera);
    g_object_unref(manager);
}
