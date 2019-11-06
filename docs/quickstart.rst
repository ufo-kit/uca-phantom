##########
Quickstart
##########

============
Installation
============

Keep in mind, that this is just a plugin interfacing the phantom cameras to the main *libuca* framework. If this frame
work is not already installed this has to be done before installing the plugin.

Prerequisites
=============

.. highlight:: bash

Obviously the `Libuca camera framework <https://github.com/ufo-kit/libuca>`_ is required to be installed prior to any
steps involving the installation of the plugin. To install the libuca framework visit its own Documentation here:
`Libuca Quickstart <https://libuca.readthedocs.io/en/latest/quickstart.html>`_.

Additionally to the C files of the uca-phantom plugin, the repository contains a Python script for testing if all the
functionality works. The script is executed as a command line tool and requires the command line library
`click <https://click.palletsprojects.com/en/7.x/>`_ to be installed. To install click use pip: ::

    pip3 install click

It is also recommended to install the Python Phantom command line tools
`phantom-cli <https://github.com/the16thpythonist/phantom-cli>`_. This python package installs several terminal
commands, which allow direct interaction with a phantom camera and also includes a *mock* command, which starts a
server on the local machine, which emulates the behaviour of a phantom camera. To read about all the features in
detail, visit the `phantom-cli Documentation <https://phantom-cli.readthedocs.io/en/latest/index.html>`_! Install
*phantom-cli* using pip: ::

    sudo pip3 install phantom-cli

**Note:** The *sudo* might be important, because sometimes the terminal commands are not properly installed without
using super user permissions.

Building on Linux
=================

First, find the folder, into which you have installed **libuca** previously onto your system. As an example we will
assume the main libuca folder is located at */home/user/libuca*. Then navigate to the folder into which you have *built*
the project, this would usually be a folder with the name *build* and then navigate to the *plugins* folder, like
this ::

    cd /home/user/libuca/build/plugins

Every plugin for libuca should have its own folder. It is easiest to obtain the uca-phantom source code by using the
*git repository*. Simply clone the repo into the plugins folder like this ::

    git clone https://fuzzy.fzk.de/gogs/jonas.teufel/uca-phantom

Now inside the newly created folder *uca-phantom* create a new folder *build* ::

    cd uca-phantom
    mkdir build

And now it is basically time to build the source code already. To do that, go into the build folder run *cmake* first
and then install the project: ::

    cd build
    cmake ..
    make
    sudo make install

**Note:** Here again the *sudo* is important, because a "normal" user usually lacks the permissions to run a
"make install" command!.

And that should be it for the basic install.

============
The Hardware
============

The *uca-phantom* plugin is build to interface with a phantom camera using a ethernet connection, but
to interface with a phantom camera some additional steps for configuring the network environment are needed.

**Note:** Usually a camera will support at least a "normal" ethernet connection to manage the control commands as well
as the image data transmission, but some cameras support an additional *10G ethernet interface*. Both of them vary in
their capacities and their setup and thus will be explained both separately.

Normal ethernet connection
==========================

1. Most phantom cameras have a fix IP address of the format *100.100.xxx.xxx* where the x's are substituted by a
different IP for every camera. To communicate with them, the machine attached must have a static IP address in the same
IP range. It is recommended to use the *IP 100.100.100.1*. Additionally the netmask of the connection PC has to be set
to *255.255.0.0*.

To make these configurations on ubuntu for example, you need to modify the file */etc/network/interfaces* like this ::

    iface eth0 inet static
    address 100.100.100.1
    netmask 255.255.0.0

(Check the identifier of your ethernet interface to which you want to connect the phantom physically by using the
*ifconfig* command, when it is not called "eth0")

2. Connect the phantom camera with your chosen network interface and turn on the camera. After the cameras *boot* time
(indicated by heavy cooler noise) the camera is ready to communicate.

10G ethernet connection
=======================

1. Most phantom cameras have a fix IP address of the format *172.16.xxx.xxx* where the x's are substituted by a
different IP for every camera. To communicate with them, the machine attached must have a static IP address in the same
IP range. It is recommended to use the *IP 172.16.1.1*. Additionally the netmask of the connection PC has to be set
to *255.255.0.0*.

To make these configurations on ubuntu for example, you need to modify the file */etc/network/interfaces* like this ::

    iface eth0 inet static
    address 172.16.1.1
    netmask 255.255.0.0

(Check the identifier of your ethernet interface to which you want to connect the phantom physically by using the
*ifconfig* command, when it is not called "eth0")

2. Connect the phantom camera's 10G ethernet port with your chosen network interface of your machine and turn on the
camera. After the cameras *boot* time (indicated by heavy cooler noise) the camera is ready to communicate.

OPTIONAL: Testing the connection
================================

If you have installed the *phantom-cli* python package, you can use the *ph-test* command to verify a successful
connection with the camera. Simply run the following in the terminal ::

    ph-test --log=DEBUG <PHANTOM IP>

If the connection is successful the output of the script will say so.

===========
Basic Usage
===========

The libuca framework is a C framework and thus the main use case for the phantom plugin is also from within a C program.
But libuca also exposes its API to several other programming languages, most prominently Python, from where a
access to the functionality is also possible.

Basic C program
===============

The first thing to do when writing a C program to utilize the libuca framework is to include the necessary headers.

.. code-block:: c

    #include <glib-object.h>
    #include <uca/uca-plugin-manager.h>
    #include <uca/uca-camera.h>

Then inside the main function, you first have to setup the plugin manager object and then use this object to create a
new camera object of the type *"phantom"*. For further details on the basic setup consult the
`Libuca Quickstart <https://libuca.readthedocs.io/en/latest/quickstart.html>`_.
To connect to the camera, set the ``connect`` property of the object to True.
Call the *start_recording* command to start the threads that will accept the incoming data connections.
And only after the camera is connected the *grab* command can be used to get individual imaged from the camera.

.. code-block:: c

    int main (int argc, char *argv[]) {
        // Declaring the variables to be used
        UcaPluginManager *manager;
        UcaCamera *camera;
        GError *error;
        gchar *c = "";

        // Creating the camera object
        manager = uca_plugin_manager_new();
        camera = uca_plugin_manager_get_camera(manager, "phantom", &error, c);

        // Connecting to the camera and starting the readout threads
        g_object_set(G_OBJECT(camera), "connect", TRUE, NULL);
        uca_camera_start_recording(camera, &error);

        // Reading out the x and y size of the region of interest (roi)
        // the NULL marks the end.
        guint16 roi_width;
        guint16 roi_height;
        g_object_get(G_OBJECT(camera), "roi-width", &roi_width, "roi-height", &roi_height, NULL);

        // Grabbing a single frame from the camera
        gpointer buffer = g_malloc0((int) roi_width * roi_height * 2);
        uca_camera_grab(camera, buffer, &error);

        // Cleaning up!
        uca_camera_stop_recording(camera, &error);
        g_object_unref(camera);
        g_free(buffer);
    }

Basic Python program
====================

**Note:** For libuca to work with Python you first need to make sure to have the library *PyGObject* installed. To
install it visit the `Documentation <https://pygobject.readthedocs.io/en/latest/getting_started.html#ubuntu-logo-ubuntu-debian-logo-debian>`_.

First you need to setup the PyGObject environment and import the *Uca* repository from it

.. code-block:: python

    # This is a workaround for the beginning, to avoid having to create a new environmental variable permanently.
    # The actual path might vary on your system
    import os
    os.environ['GI_TYPELIB_PATH'] = '/usr/local/lib/girepository-1.0'

    import gi
    gi.require_version('Uca', '2.0')
    from gi.repository import Uca

As with the C code, you first have to create the plugin manager object and from that you can request the camera object.
Then you can use the function *create_array_from* to grab a frame from the camera

.. code-block:: python

    # Just copy the function
    def create_array_from(camera):
        """Create a suitably sized Numpy array and return it together with the
        arrays data pointer"""
        bits = camera.props.sensor_bitdepth
        dtype = np.uint16 if bits > 8 else np.uint8
        a = np.zeros((camera.props.roi_height, camera.props.roi_width), dtype=dtype)
        return a, a.__array_interface__['data'][0]

    if __name__ == '__main__':
        # Creating the plugin manager object and the camera object
        plugin_manager = Uca.PluginManager()
        camera = plugin_manager.get_camerav('phantom', [])

        # Connecting the camera and starting the readout threads
        camera.props.connect = True
        camera.start_recording()

        # Grabbing a frame
        a, buf = create_array_from(camera)
        camera.grab(buf)
        # >> a will now contain the numpy array with the image

        # Clean up
        camera.stop_recording()
