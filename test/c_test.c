#include <uca-camera.h>
#include <uca-plugin-manager.h>
#include <glib-object.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>

int main(void) {
    GError *error;
    guint bits = 0, roi_width=0, roi_height=0;
    gpointer buffer = NULL;
    errno = 0;
    // char filename[256] = "/home/ws/lt0649/Documents/uca-phantom/build/image.txt";
    char filename[256] = "image.txt";
    
    // Set the linux env variables
    g_setenv ("PH_NETWORK_INTERFACE", "enp4s0f1", TRUE);
    g_setenv ("PH_NETWORK_ADDRESS", "", TRUE);

    // Find and create the camera object
    UcaPluginManager *plugin_manager = uca_plugin_manager_new();
    GList *cameras = uca_plugin_manager_get_available_cameras (plugin_manager);
    gchar *camera_name = (gchar *) g_list_nth_data (cameras, 1);
    UcaCamera *camera = uca_plugin_manager_get_camera
        (plugin_manager, camera_name, &error, NULL);
    
    g_object_set(camera, "enable-10ge", TRUE, NULL);
    // g_object_set(camera, "enable-memread", TRUE, NULL);
    // g_object_set(camera, "image-format", 2, NULL);
    // g_object_set(camera, "network-interface", "enp4s0f1", NULL);
    // g_object_set(camera, "network-address", "172.16.0.1", NULL);
    g_object_get (camera, "sensor-bitdepth", &bits, "roi-width", &roi_width, "roi-height", &roi_height, NULL);

    // Setup the buffer
    buffer = g_malloc0 ((guint16) 4* ( roi_width*roi_height ) );

    // Connect to the camera, start recording
    g_object_set(G_OBJECT(camera), "connect", TRUE, NULL);
    uca_camera_start_recording (camera, &error);
    uca_camera_grab (camera, buffer, &error);

    // FILE *image = fopen (filename, "rw");
    // if (image == NULL) {
    //     g_print ("failed making file, errno: %d\n", errno);
    //     return -1;
    // }

    guint16 *image_data = buffer;
    guint row = 0;
    printf ("%d\n", roi_height);
    printf ("%d\n", roi_width);

    for (guint i = 0; i < (roi_width)*(roi_height) ; i++) {
        // row = i*roi_height;
        // for (guint j = 0; j < roi_height/2.0; j++) {
        //     // grey level + alpha channel ?
        //     printf ("%d ", image_data[row+j]);
        // }
        // printf ("\n");
        // printf ("%d\n", image_data[i]);
    }

    // fclose (image);

    uca_camera_stop_recording (camera, &error);


    g_object_unref (plugin_manager);
    g_list_free_full (cameras, g_free);
    g_object_unref (camera);
    g_free (buffer);

    return 0;
} 