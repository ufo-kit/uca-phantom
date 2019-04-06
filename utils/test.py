#!/usr/bin/env python3
# Using the PyGObject package (gi.repository) it is important, that it can find the necessary typedef files for the
# "libuca" file, for that the path to those files needs to be listed in the "GI_TYPEDEF_PATH" environment variable.
# Setting this variable here is merely a workaround.
import os
os.environ['GI_TYPELIB_PATH'] = '/usr/local/lib/girepository-1.0'

import gi
gi.require_version('Uca', '2.0')
from gi.repository import Uca


pm = Uca.PluginManager()

print(pm.get_available_cameras())

camera = pm.get_camerav('phantom', [])

import numpy as np
import matplotlib.pyplot as plt


def create_array_from(camera):
    """Create a suitably sized Numpy array and return it together with the
    arrays data pointer"""
    bits = camera.props.sensor_bitdepth
    dtype = np.uint16 if bits > 8 else np.uint8
    a = np.zeros((camera.props.roi_height, camera.props.roi_width), dtype=dtype)
    return a, a.__array_interface__['data'][0]


# Suppose 'camera' is a already available, you would get the camera data like
# this:
a, buf = create_array_from(camera)

try:
    camera.start_recording()
    camera.grab(buf)
except KeyboardInterrupt:
    exit()

# Now data is in 'a' and we can use Numpy functions on it
print(np.mean(a))
print(a)
a[a > 255] = 255
plt.imshow(a, cmap='gray')
plt.show()

camera.stop_recording()