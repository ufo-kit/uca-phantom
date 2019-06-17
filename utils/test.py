#!/usr/bin/env python3

# python standard library imports
import logging
import collections
import os
os.environ['GI_TYPELIB_PATH'] = '/usr/local/lib/girepository-1.0'


# third party import
import click
import numpy as np
import matplotlib.pyplot as plt

import gi
gi.require_version('Uca', '2.0')
from gi.repository import Uca

# ################
# GLOBAL VARIABLES
# ################

# LOGGING RELATED VARIABLES
# -------------------------

LOG_HELP = "The log level to be displayed as output of the script. The options are ERROR to only display a message " \
           "in case of an error, INFO to display general infos about the progress of the testing and DEBUG to even " \
           "display small steps of the program. DEFAULT is INFO"
LOG_FORMAT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
LOG_DEFAULT = "INFO"
LOG_OPTIONS = {
    'DEBUG':        logging.DEBUG,
    'INFO':         logging.INFO,
    'ERROR':        logging.ERROR
}
LOG_CONFIG = collections.defaultdict(lambda: LOG_OPTIONS[LOG_DEFAULT], **LOG_OPTIONS)


# NETWORKING RELATED
# ------------------

IP_HELP = "A IP address for the camera can be manually supplied, if the UDP discovery mode of the camera is NOT to be " \
          "used. The supplied IP address will be used instead. (This is for example necessary, if testing with the " \
          "mock server). DEFAULT is '' (empty string will signal to use discovery)"
INTERFACE_HELP = "If using the camera in 10G ethernet mode, the string identifier name of the ethernet interface " \
                 "to which the camera is connected has to be supplied additionally. Only takes effect if the " \
                 "--xnetwork flag is set. DEFAULT is 'eth0'"
INTERFACE_DEFAULT = 'eth0'
X_NETWORK_HELP = "This is a boolean flag, that can be set to signify, that the camera is connected using the 10G " \
                 "ethernet connection"

# CAMERA RELATED VARIABLES
# ------------------------

CAMERA_IDENTIFIER = 'phantom'


# ################
# HELPER FUNCTIONS
# ################

def create_array_from(camera):
    """Create a suitably sized Numpy array and return it together with the
    arrays data pointer"""
    bits = camera.props.sensor_bitdepth
    dtype = np.uint16 if bits > 8 else np.uint8
    a = np.zeros((camera.props.roi_height, camera.props.roi_width), dtype=dtype)
    return a, a.__array_interface__['data'][0]


# ##################
# THE ACTUAL COMMAND
# ##################


@click.command('test')
@click.option('--xnetwork', '-x', is_flag=True, help=X_NETWORK_HELP)
@click.option('--interface', '-e', default=INTERFACE_DEFAULT, help=INTERFACE_HELP)
@click.option('--ip', '-i', default="", help=IP_HELP)
@click.option('--log', '-l', default=LOG_DEFAULT, help=LOG_HELP)
def command(log, ip, interface, xnetwork):
    # First we set up the logging for the command
    loggisetng.basicConfig(
        format=LOG_FORMAT,
        level=LOG_CONFIG[log]
    )
    logger = logging.getLogger('test.py')

    # Creating the plugin manager and the camera object
    plugin_manager = Uca.PluginManager()
    logger.debug("Created the plugin manager")
    camera = plugin_manager.get_camerav(CAMERA_IDENTIFIER, [])
    logger.debug("Created the camera object")

    # Setting the given ip address to be used for the phantom camera.
    # The default value is an empty string. Supplying an empty string will tell the camera to use the automatic
    # UDP discovery protocol instead.
    camera.props.network_address = ip
    logger.debug("Attempting to connect to phantom at IP %s", ip)
    # If the 10G connection is being used, a boolean flag has to be set on the camera and additionally the interface
    # name to which the camera is connected has to be supplied
    if xnetwork:
        camera.props.network_interface = interface
        camera.props.enable_10ge = True
        logger.debug("Using 10G network, connected on INTERFACE '%s'", interface)

    # Connecting the camera
    camera.props.connect = True
    logger.info("Camera connected!")

    # Starting the readout
    camera.start_recording()
    logger.info("Readout threads started!")

    # Grabbing a frame for testing and then displaying it
    a, buf = create_array_from(camera)
    camera.grab(buf)
    logger.info("Frame acquired")

    plt.imshow(a, cmap="gray")
    plt.show()
    logger.debug("Plot of the image has been closed by the user")

    # Cleaning up - Stopping the camera connection
    logger.debug("Stopping tze camera connection")
    camera.stop_recording()


if __name__ == '__main__':
    command()
