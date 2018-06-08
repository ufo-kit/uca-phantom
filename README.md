## uca-phantom

[libuca](https://github.com/ufo-kit/libuca) plugin for the Vision Research
Phantom high-speed cameras.

### User information

#### Changing the acquisition mode

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


#### Transfering data with a 10GE connection

Due to the way the protocol works, make sure to set the 10Gb network interface
via the `network-interface` property and set `enable-10ge` to true. Note that
those values are ephemeral and will be gone once you create a new camera object.
Furthermore, you must make sure that your application is able to open raw
sockets. Either run it as root (not recommended) or set the `cap_net_raw`
capability on the binary:

    $ setcap cap_net_raw+ep <executable>


#### Testing with the mock software emulation

Go to the `utils` directory and start the UDP discovery and TCP communication
servers by running the `mock` script. The `phantom` plugin should be able to
communicate with this replacement.


### Developer information

Here are some information in which this plugin differes from other camera
plugins. The biggest difference is in how data transfers are handled. Unlike
other cameras, this happens through a secondary channel for which *we* open a
socket on which we listen to connection requests and incoming data. Because we
still need to react on API calls this is handled in a separate thread that runs
`accept_img_data`. This thread and the API communicate through two asynchronous
message and return queues. The thread picks up a message and once done places a
result object on the return queue. Messages and results carry type information,
meta and real data.
