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

### 0.0.0.1 

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

