########################
Complete example program
########################

The following example program will feature the following steps:
1) Connection to the camera using the 10G interface, manually supplying IP address
2) Getting the height and width configuration of the camera
3) Acquiring frames using the software trigger
4) Readout of the acquired frames using the memread mode

==============
C program code
==============

.. code-block:: c

    #include <stdio.h>
    #include <string.h>
    #inlcude <glib-object.h>
    #include <uca/uca-plugin-manager.h>
    #include <uca/uca-camera.h>

    int main(int argc, char *argv[]) {
        // Declaring the plugin manager and the camera object. The plugin manager will be needed to get the "phantom"
        // version of the camera from the phantom plugin
        UcaPluginManager *manager;
        UcaCamera *camera;

        GError *error = NULL;
        gchar *c = "";

        manager = uca_plugin_manager_new();
        camera = uca_plugin_manager_get_camera(manager, "phantom", &error, c);

        // 1)
        // Now the IP address of the phantom has to be set, as well as the interface name of the ethernet interface of
        // this machine, with which the camera is connected. The flag "enable-10g" tells the plugin to use special
        // sockets for data transmission
        g_object_set(G_OBJECT(camera), "network-interface", "eth0", NULL);
        g_object_set(G_OBJECT(camera), "network-address", "172.16.33.157", NULL);
        g_object_set(G_OBJECT(camera), "enable-10g", TRUE, NULL);

        // Setting the "connect" flag to true, will internally call the connect method which established the control
        // connection. The "start_recording" function starts the threads which handle the incoming data
        g_object_set(G_OBJECT(camera), "connect", TRUE, NULL);
        uca_camera_start_recording(camera, &error);

        // 2)
        // "region of interest"-heigh/width of the camera are internally mapped as guint16 values
        guint16 width;
        guint16 height;
        // Both values can be acquired with one operation, the NULL passed at the end signals when the parameters end,
        // not a fix amount of parameters. Obviously the references to these variables have to be passed, to that the
        // method can modify them
        g_object_get(G_OBJECT(camera), "roi-width", &width, "roi-height", &height, NULL);

        // 3)
        // Before actually triggering the camera, the amount of frames we actually want to be recorded as to
        // be specified. This we do, by setting the "post-trigger-frames" property
        int FRAME_COUNT = 10000;
        g_object_set(G_OBJECT(camera), "post-trigger-frames", FRAME_COUNT);
        uca_camera_trigger(camera, &error);

        // The "trigger-released" property is a boolean property of the camera, which is FALSE, when a triggered
        // process is currently active on the camera and TRUE, when the process is finished.
        // Note: every call to the property will internally send a network request to the camera basically asking it
        // if it is done yet
        gboolean released = FALSE;
        while (!released) {
            g_object_get(G_OBJECT(camera), "trigger-released", &released, NULL);
        }

        // 4)
        // To readout the frames, we first need a buffer, into which we can put the data. The size of this buffer needs
        // to be 16 bit (2 bytes) per pixel of the frames, which will be returned.
        gpointer buffer = g_malloc0((int) (height * width * 2))

        // To setup the memread mode, we first need to set the boolean flag to tell the program to switch modes,
        // and then specify the amount of frames to be read
        g_object_set(G_OBJECT(camera), "enable-memread", TRUE, NULL);
        g_object_set(G_OBJECT(camera), "memread-count", IMAGE_COUNT, NULL);

        for (int i = 0; i < IMAGE_COUNT; i++) {
            // The actual command to the camera will only be sent after the first grab call
            uca_camera_grab(camera, buffer, &error);

            // Some custom code to save the images into files...
        }
    }


===================
Python program code
===================

.. code-block:: python

    pass

