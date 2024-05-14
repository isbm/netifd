# Build

To build `netifd` you need to install required headers. Example on Ubuntu (elsewhere YMMV):

- After typical CMake, GNU Make, GCC installation, also install the following packages:
    - `libubus-dev`
    - `libubox-dev`
    - `libjson-c-dev`
    - `libuci-dev`
    - `libnl-3-dev`
- Clone this repo and change there: `git clone ....`
- Create `build` directory and change there: `mkdir build; cd build`
- Run CMake to generate Makefile: `cmake ../`
- Run Makefile: `make`

This should do.
