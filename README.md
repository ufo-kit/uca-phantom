## uca-phantom

[libuca](https://github.com/ufo-kit/libuca) plugin for the Vision Research
Phantom high-speed cameras.

### Changing the acquisition mode

The acquisition mode determines if the sensor operates in binning, standard and
HS mode. This has a few implications on the operation: first we cannot determine
in which mode the camera is currently operation, therefore you *cannot* rely on
the value of the `acquisition-mode` property. Second, binning changes the
reported maximum sensor resolution, therefore you *must* update the
region-of-interest when changing the binning. Otherwise, you will read too much
or crash by reading outside the window. You will be on the safe side, if you
just set the acquisition mode and restart any libuca application. Third,
changing modes takes about 15 seconds which might make the application appear to
be frozen.

### Transfering data with a 10GE connection

Due to the way the protocol works, make sure to set the correct MAC address on
the `mac-address` property and set `enable-10ge` to a true value. Note that
those values are ephemeral and will be gone once you create a new camera object.

### Testing with the mock software emulation

Go to the `utils` directory and start the UDP discovery and TCP communication
servers by running the `mock` script. The `phantom` plugin should be able to
communicate with this replacement.
