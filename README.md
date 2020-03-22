# Utilities for PassMark USB 3.0 loopback plug
This program allows you to use the PassMark USB 3.0 loopback plug to test USB
connections from the command line.

I wrote these utilities for my testing needs and thus the functionality is
limited.

**NOTE: I'm in no way connected to PassMark. Any question specific to the
PassMark USB 3.0 loopback plug, the PassMark Usb3Test Windows utility or any
other official PassMark products should be asked to the PassMark support.**

## Installation
This program requires LibUSB 1.0. Make sure you have the development files
installed.

To compile and install run:

    # make
    # make install

To allow running test as non-root user, place the u3loop.rules file in the
/etc/udev/rules.d directory.

## TODO

 - Switch to CMake
 - Option to enable Device error counters in benchmark mode
 - Device error counters don't always make sense. Sometimes 0 while error mask
   != 0, and vice versa.
