import numpy as np
from concert.quantities import q
from concert.devices.cameras.uca import Camera


camera = Camera("mock")
#camera.network_address = "127.0.0.1"
#camera.network_interface = "enp1s0"
#camera.enable_10ge = True

camera.start_recording()

frame = camera.grab()
print(frame)

camera.stop_recording()
