
##################################
Software trigger to acquire frames
##################################

On some occasions it might be more useful to let the camera record a bunch of frames with the maximum frame rate
into its own internal memory and then read out these frames later on.

This can also be done by code, using the software trigger functionality of the phantom camera. When the camera
receives a trigger command it will start to record as many frames as previously specified into its own memory.

To issue such a trigger using the phantom plugin, 3 steps are involved:

1) Specify the amount of frames to be recorded: To do this, set the ``post-trigger-frames`` property of the camera
object to the desired integer amount of frames

2) Actually issue a software trigger by using the ``trigger`` function of the libuca framework

3) Wait until the recording is finished. By checking the boolean property ``trigger-released`` of the camera object
you can check at any given moment if the camera is done recording. It is important to wait until the camera is
finished. If the recording is not finished and the images are requested anyways the camera attempts to access a locked
memory partition, resulting in an error.

.. note::

    When the camera receives the trigger command, it will implicitly delete its entire memory before recording the new
    frames, so trigger the camera unless all important data has been read out already!

C example:

.. code-block:: c

    // Setting up the camera...

    // 1 - Setting the post trigger frames
    g_object_set(G_OBJECT(camera), "post-trigger-frames", 1000, NULL);
    // 2 - Actually issuing the trigger command
    uca_camera_trigger(camera, &error);
    // 3 - Waiting for the recording to finish
    gboolean released = FALSE;
    while (!released) {
        g_object_get(G_OBJECT(camera), "trigger-released", &released, NULL);
    }

    // Readout of the recording...

Python example:

.. code-block:: python

    # Setting up the camera...

    # 1 - Setting the post trigger frames
    camera.props.post_trigger_frames = 1000
    # 2 - Actually issuing the trigger command
    camera.trigger()
    # 3 - Waiting for the recording to finish
    released = False
    while not released:
        released = camera.props.trigger_released

    # Readout of the recording...