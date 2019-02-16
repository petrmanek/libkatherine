Katherine Control Library
=========================

The Katherine control library contains a working implementation of the
UDP-based communication protocol. It may be used to control and receive
data from the Katherine readout.

This git repository contains 3 libraries in total:

 1. [libkatherine](./c/), a C library,
 2. [libkatherinexx](./cxx/), a C++ header-only wrapper,
 3. [katherine](./python/), a Python wrapper package


## Usage

### Getting Started

The following simple code snippets in C, C++ and Python, respectively,
show the intended usage of the library. The code prints the chip ID of
a read-out at a given IP address.

```c
// C example
#include <stdio.h>
#include <katherine/katherine.h>

const char *ip_addr = "192.168.1.142";

katherine_device_t dev;
katherine_device_init(&dev, ip_addr);   // Ignoring return code.

char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
katherine_get_chip_id(&dev, chip_id);   // Ignoring return code.
printf("Device %s has chip id: %s\n", ip_addr, chip_id);

katherine_device_fini(&dev);
```

```cpp
// C++ example
#include <iostream>
#include <katherinexx/katherinexx.hpp>

const std::string ip_addr{"192.168.1.142"};

katherine::device dev{ip_addr};
const std::string chip_id = dev.chip_id();   // Exception can be thrown here.
std::cout << "Device " << address << " has chip id: " << chip_id << std::endl;
```

```python
# Python example
from katherine import Device

ip_addr = '192.168.1.142'

dev = Device(ip_addr)
chip_id = dev.get_chip_id()   # OSError can be raised here.
print('Device %s has chip id: %s' % (ip_addr, chip_id))
```

### More Examples

To show advanced usage of all provided libraries, several commented example
programs and scripts are included in the repository. They can be either found
in the `examples/` subdirectory for each library, or in the table below:

| C                             | C++                                   | Python                                    | Purpose                                              |
|-------------------------------|---------------------------------------|-------------------------------------------|------------------------------------------------------|
| [kfind](./c/examples/kfind.c) | [kfindxx](./cxx/examples/kfindxx.cpp) | [kfind.py](./python/examples/kfind.py)    | Locate Katherine readouts in given IP address range. |
|                               | [krunxx](./cxx/examples/krunxx.cpp)   |                                           | Configure & perform data-driven acquisition.         |


### Full Documentation

The contents of the C library use in-code and Javadoc-style documentation.
Pre-built documentation may be found in the `docs/` directory. Upon changes,
the Doxygen tool can recreate its contents.

High-level overview may be found in the Chapter 3 of the thesis.


### Wrappers

For the reasons of redundancy, the provided wrappers are deliberately _not_
documented. Since their programming interface models that of libkatherine,
corresponding functions can be easily identified (usually just by adding the
prefix `katherine_`).


## Build Notes

The project uses CMake 3 build system. It can be built as follows:

```shell
mkdir build && cd build
cmake ..
make
```

_(instead of Makefiles, different build tool can be used, e.g. ninja)_


### C library (libkatherine)

The C library uses the following dependencies:

 - C11 standard library,
 - POSIX threads,
 - BSD socket interface.

For that reason, it is currently supported *only* on \*nix systems.


### C++ wrapper

The C++ wrapper uses the following dependencies:

 - C++14 standard library,
 - libkatherine (the C library)

Since the wrapper is header-only, there are no produced binaries and all calls
are directly forwarded to libkatherine.


### Python wrapper

The Python wrapper uses the following dependencies:

 - Python 3.5,
 - Cython compiler 0.29,
 - libkatherine (the C library)

The wrapper produces a shared library, which can be loaded from PYTHONPATH
in any script.


## Copyright

&copy; Petr Mánek 2018, All rights reserved.

Contents of this library are provided for use under the conditions of the
MIT License (see `LICENSE`).


### Citing

If you use this library in your academic work, please make sure you include
a correct citation of [my thesis][thesis], in which was this library originally
developed and tested.

If you use BibTeX, you can use the following code:

```bibtex
  @THESIS{Manek2018_CUNI,
    author={P. Mánek},
    title={A system for 3D localization of gamma sources using Timepix3-based Compton cameras},
    year={2018},
    institution={Faculty of Mathematics and Physics, Charles University},
    type={Master's thesis}
  } 
```


### Contributors

I would like to thank the following people and institutions for their help
in the development of this library:

 - Petr Burian, University of West Bohemia,
 - Jan Broulím, Institute of Experimental and Applied Physics CTU,
 - Lukáš Meduna, Institute of Experimental and Applied Physics CTU,
 - Jakub Begera, Institute of Experimental and Applied Physics CTU,
 - Felix Lehner, Physikalisch-Technische Bundesanstalt.


[thesis]: http://hdl.handle.net/20.500.11956/101404
