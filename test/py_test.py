import os
os.environ['GI_TYPELIB_PATH'] = '/usr/local/lib/girepository-1.0'

import gi
gi.require_version('Uca', '2.0')
from gi.repository import Uca

# complete program shortened ...
plugin_manager = Uca.PluginManager()
camera = plugin_manager.get_camerav('phantom', [])

# Connecting the camera and starting the readout threads
camera.props.connect = True
camera.start_recording()
camera.stop_recording()