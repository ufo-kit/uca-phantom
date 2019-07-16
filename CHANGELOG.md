## CHANGELOG

### FORK - 10.03.2019

- The project has been forked from 
[this GOGS page](https://fuzzy.fzk.de/gogs/UFO-libuca/uca-phantom)
All the following versions will be considered the versions of the fork 
and not the project as a whole

### 0.0.0.0 - 10.03.2019

- Added the definition of the C99 standard to the makefile to solve a 
build error on unix systems
- The main file has been modified to not use the UDP discovery protocol 
of the phantom, but instead the ip address has to be hardcoded into t
he file. This is a temporary workaround and will be changed int the future
- Added a "test.py" script into the utils folder, which will use the 
python binding of the libuca package to test if it is possible to grab 
frames using this plugin. (The libuca and phantom plugin have to be 
build on the machine for that!)
- Changed the "mock" script to use the special library 
[phantom-cli](https://github.com/the16thpythonist/phantom-cli)

### 0.0.0.1 - 27.05.2019

- Reworked the implementation for the transmission of 10G imaga data. 
The previous implementation used standard socket read operations to wait 
for incoming data. This proved to be to slow for 10G ethernet 
transmission rates reaching up to ~800 MB/s.
The new implementation uses the linux library 
[packet mmap](https://www.kernel.org/doc/Documentation/networking/packet_mmap.txt).
The transmission works by setting up a ring buffer, which is directly 
memory mapped into the kernel space, where the actual ethernet packets 
are being stored. In the the receive-thread loop, these packets are 
then being added to a local buffer for the complete image data.
- Implemented a decoding algorithm for the 10G data transmission format.
Using the 10G connection to the phantom camera, the P10 transfer format 
is being used. This format encodes each pixel value into 10 bit, but 
the libuca framework expects the output buffer to consist of 16 bit 
integer values per pixel.
(At the moment this part of the program limits the transmission speed, 
as the algorithm for the decoding is too slow)
- Reworked the way the decoding is handled, when. Previously the 
decoding happened after the complete image data was been received and 
before the data was copied into the final output buffer. Now the 
decoding runs step by step in a separate thread, in an attempt to 
squeeze out some additional performance.
- Created a new decoding algorithm, which uses SSE4 intrinsics to speed 
up the unpacking process of the 10 bit pixel data stream into the 
16 bit output buffer.
- Reworked the /utils/test.py to use the python command line 
library [Click](https://click.palletsprojects.com/en/7.x/). The new 
test script now also accepts parameters, that maintain compatibility 
with the phantom-cli mock server.

### 0.0.0.2 - 29.05.2019

- Fixed a bug, where single packages would disappear during 10G 
transmission when the edge case occurred, that an image was finished 
with the pre last packet in a block of the ring buffer.
- Fixed the bug with the "trigger" method not containing the "rec" 
command being send to the camera previous to the "trig" command, which 
is being expected by the camera.

### 0.1.0 - 18.05.2019

- Removed the debugging messages
- First stable version to be used in production

### 0.1.1 - 26.06.2019

- Changed the Chunk size for the 10G Transmission from 400 to 100, as 
400 worked fine with 1000ish width frames, but using the 2000ish width 
frames, they have too many bytes and cause a ring buffer overflow
- Added the ability to configure the connection to the phantom using 
OS environmental variables, which are being read out during the init of 
the camera object. This change has been made to create compatibility 
with uca tools such as "uca-grab" and "uca-info", which do not provide 
the ability to define the connection parameters in code, before 
attempting to interact with the camera object
 - PH_NETWORK_ADDRESS can be defined to provide the IP address during 
 the init of the camera object. If this env variable is defined the 
 connection to the phantom is established during the init of the camera 
 object as well. There is no further need to set the "connect" flag.
 - PH_NETWORK_INTERFACE can be defined to pass the string identifier of 
 the ethernet interface onto which the 10G port of the phantom is 
 connected. If this variable is defined, the camera object will 
 automatically be set to 10G mode "enable_10g" flag to TRUE.
- Fixed a Bug, where the "PROP_NETWORK_ADDRESS" is a read write 
property of the camera object, but does not have a read option defined.
- Removed the makros at the start of the code, which defined hardcoded 
network configuration.

### 0.1.2 - 30.06.2019

- Fixed a bug, where the program could not be compiled due to syntax 
error
- Added support for the "aux-mode" property of the phantom camera. It 
can be set with an integer from 0-2 to set the function, which the 
first auxiliary port of the camera will serve

### 0.1.3 - 10.06.2019

- Moved the "rec" command, which is being used for the trigger command 
into its own function, so that it can be used separately in the future, 
as it turned out it will be important for hardware triggers as well.

### 0.1.4 - 14.07.2019

- Added "P12L" transfer format support for the 1G transmission
- Added "P12L" transfer format support for the 10G transmission
 - Creates a new algorithm using the SSE instructions tp provide 
 sufficient processing speed for 10G reception of frames
- The "fmt" parameter is now being sent with the "ximg" command as 
well.
- Updated the test.py script
 - IP address and Interface for 10G connection are now being passed to 
 the phantom plugin by setting the environmental variables
 - Added the "format" option to the script, where either "P12L" or 
 "P10" can be selected.
 
### 0.1.5 - 16.07.2019
 
 - Added the additional camera property "external-trigger", which is a 
 boolean flag, that indicates, whether the use of external triggering 
 is to be enabled during the next recordings
 - The "start_recording" method will now send the necessary "rec" 
 command as a preparation to the camera, so that external triggering 
 will work