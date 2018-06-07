## uca-phantom

[libuca](https://github.com/ufo-kit/libuca) plugin for the Vision Research
Phantom high-speed cameras.

### Transfering data with a 10GE connection

Due to the way the protocol works, make sure to set the correct MAC address on
the `mac-address` property and set `enable-10ge` to a true value. Note that
those values are ephemeral and will be gone once you create a new camera object.

### Testing with the mock software emulation

Go to the `utils` directory and start the UDP discovery and TCP communication
servers by running the `mock` script. The `phantom` plugin should be able to
communicate with this replacement.
