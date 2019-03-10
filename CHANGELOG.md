## CHANGELOG

### FORK - 10.03.2019

- The project has been forked from [this GOGS page](https://fuzzy.fzk.de/gogs/UFO-libuca/uca-phantom)
All the following versions will be considered the versions of the fork and not the project as a whole

### 0.0.0.0 -10.03.2019

- Added the definition of the C99 standard to the makefile to solve a build error on unix systems
- The main file has been modified to not use the UDP discovery protocol of the phantom, but instead the ip 
address has to be hardcoded into the file. This is a temporary workaround and will be changed int the future
- Added a "test.py" script into the utils folder, which will use the python binding of the libuca package to test 
if it is possible to grab frames using this plugin. (The libuca and phantom plugin have to be build on the machine 
for that!)
- Changed the "mock" script to use the special library [phantom-cli](https://github.com/the16thpythonist/phantom-cli)