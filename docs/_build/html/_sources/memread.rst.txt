################
The MEMREAD mode
################

.. note::

    The memread mode will only work, if the camera is configured in 10G at the moment!

==========================
The default operating mode
==========================

On default the ``grab`` method for the camera object will return the *current* frame of the camera,
as in the image, which it "sees" *just at that moment* when the grab call is being made.
This default mode however lacks speed, because if multiple frames have to be acquired from the
camera the following pattern will have to be repeated:

1. Request for a frame is sent to the camera
2. Camera *takes the picture first* and sends the response
3. Client receives the image data
4. New request is sent...

=====================
The fast memread mode
=====================

This default behaviour is slow and might not even be needed. The **memread mode** offers a fast
way of reading images from the internal memory of the camera, that have been takes previously
(for example using a software trigger).

To switch from the normal mode into the memread mode set the boolean property ``enable-memread`` of the camera
object to TRUE. Then set the ``memread-count`` property of the camera object to the integer amount of frames to be
read out from the internal memory of the camera.

C example:

.. code-block:: c

    // Setting up & connecting the camera...
    int FRAME_COUNT = 1000;

    g_object_set(G_OBJECT(camera), "enable-memread", TRUE, NULL);
    g_object_set(G_OBJECT(camera), "memread-count", FRAME_COUNT, NULL);

    for (int i = 0; i < FRAME_COUNT; i++) {
        uca_camera_grab (camera, buffer, &error);
    }

Python example:

.. code-block:: python

    # Setting up & connecting the camera...
    FRAME_COUNT = 1000

    camera.props.enable_memread = 1000
    camera.props.memread_count = FRAME_COUNT

    for i in range(FRAME_COUNT):
        camera.grab()

.. note::

    When using the memread mode make sure, that the grab method is being called exactly as many times as specified
    in the *memread-count* property. Doing otherwise will result the program crashing!


A note on the Python bindings
=============================

The memread mode generally works the same way using Python. The order of things to be done in the
program are:

1. Setup the camera object
2. Setup the camera network properties for 10G connection
3. Connect to the camera, by setting the ``connect`` property to True
3. Start the readout threads by calling ``start_recording`` method of the camera object
4. Record frames using the ``trigger`` method
5. Set all the according properties to enable and configure the memread mode
6. Call the ``grab`` method *exactly* as many times as specified


