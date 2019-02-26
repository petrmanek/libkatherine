Katherine Control Library
=========================

The Katherine control library contains a working implementation of
UDP-based communication protocol. It may be used to control and receive
data from Timepix3 using Katherine readouts.

This git repository contains 3 libraries in total:

 1. [libkatherine](./c/), a C library,
 2. [libkatherinexx](./cxx/), a C++ header-only wrapper,
 3. [katherine](./python/), a Python wrapper package.

At the present time, the library is **multi-platform**. The implementation
supports the following platforms:

Platform | CI Status
---------|:---------
Linux    | [![Linux Build Status][travis-badge-linux]][travis]
macOS    | [![macOS Build Status][travis-badge-osx]][travis]
Windows  | [![Windows Build Status][travis-badge-windows]][travis]


## Usage

### Getting Started

The following simple code snippets in C, C++ and Python, respectively,
show the intended usage of the library. The code prints the chip ID of
a read-out at a given IP address.

```c
// C example
#include <stdio.h>
#include <katherine/katherine.h>

int main() {
  const char *ip_addr = "192.168.1.142";

  katherine_device_t dev;
  katherine_device_init(&dev, ip_addr);   // Ignoring return code.

  char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
  katherine_get_chip_id(&dev, chip_id);   // Ignoring return code.
  printf("Device %s has chip id: %s\n", ip_addr, chip_id);

  katherine_device_fini(&dev);
}
```

```cpp
// C++ example
#include <iostream>
#include <katherinexx/katherinexx.hpp>

int main() {
  const std::string ip_addr{"192.168.1.142"};

  katherine::device dev{ip_addr};
  const std::string chip_id = dev.chip_id();   // Exception can be thrown here.
  std::cout << "Device " << address << " has chip id: " << chip_id << std::endl;
}
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
| [krun](./c/examples/krun.c)   | [krunxx](./cxx/examples/krunxx.cpp)   | [krun.py](./python/examples/krun.py)      | Configure & perform data-driven acquisition.         |


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

The project uses CMake 3 build system. It can be configured, built and installed
by standard CMake commands. In case of doubt, check the [Travis][travis-yml]
configuration file for examples of build commands for individual platforms.

For convenience, here's a minimal out-of-source-directory build script example:

```shell
mkdir build && cd build
cmake ..
make
```

_(note that in CMake projects, different build tools can be used instead of
GNU Makefiles, e.g. ninja)_

The CMake project also defines several options. They can be defined in the CMake
cache, by environment variables or using the `-D<option>=<value>` options.

Option            | Default Value | Meaning
------------------|---------------|-------------------------------------------------------
`BUILD_CXX`       | `ON`          | Enables building C++ binaries (see requirements)
`BUILD_PYTHON`    | `OFF`         | Enables building Python extension (see requirements)
`BUILD_EXAMPLES`  | `ON`          | Enables building example programs

For optimal performance, consider also configuring standard CMake options such as
`CMAKE_BUILD_TYPE` which configures the compiler optimization policies or
include additional debug information. See [CMake docs][cbt-doc] for more information.


### C library (libkatherine)

The C library uses the following dependencies:

 - C11 standard library,
 - Version for \*nix systems:
   - POSIX threads (pthread),
   - BSD socket interface,
 - Version for Win32 systems:
   - Windows Sockets API (WSA) 2.2 (in ws2_32.dll),
   - Windows Synchronization Primitives (in kernel32.dll).


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

The wrapper generates an extension module which can be loaded and used by any script.
Its file name is derived from platform and Python version. Upon successful build, the
file can be located inside the CMake build directory at path:
`./python/build/lib.{PLATFORM}-{ARCH}-{PYTHON_VERSION}/`. While in Linux systems, the
file has .so extension (e.g. `katherine.cpython-37m-x86_64-linux-gnu.so`), in Windows
the file's extension is .pyd (e.g. `katherine.cp37-win_amd64.pyd`).

**Note:** Before using the Python wrapper, make sure that the interpeter has access to all
the required files. Specifically:

 1. The extension module is located in one of the `PYTHONPATH` directories.
 2. The `libkatherine.so` library file (or `katherine.dll` in Windows) is located in one
    of the `LD_LIBRARY_PATH` directories (or `PATH` directories in Windows).

If these conditions are not satisfied, you are likely going to encounter to `ModuleNotFoundError`
in the first case and `ImportError` in the second.

Be also aware that you can change the variables directly from Python without having to
alter their values on system-wide level. This is especially useful in Windows environments. Here's
an example script:

```python
import sys
import os

ext_path = '<directory containing extension module>'
lib_path = '<directory containing katherine library file>'

# Alter environment to include the extension module
sys.path.append(ext_path)

# Alter environment to include the library
if os.name == 'nt':
  # use semicolon on Windows systems
  os.environ['PATH'] += ';%s;' % lib_path
else:
  # use different variable and colon on *nix systems
  os.environ['LD_LIBRARY_PATH'] += ':%s:' % lib_path

try:
  import katherine
  dev = katherine.Device('192.168.1.145')
except ModuleNotFoundError:
  print('Something wrong with ext_path')
except ImportError:
  print('Something wrong with lib_path')
```

If you get linker errors during Cython build phase, check that the target architectures of
the katherine library and the python extension modules are the same. In Windows environment,
Cython prefers 64-bit MSVC by default, so it is necessary to choose the "Win64" generator
in CMake configuration.


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

[travis]:               https://travis-ci.org/petrmanek/libkatherine
[travis-yml]:           ./.travis.yml
[travis-badge-linux]:   https://badges.herokuapp.com/travis/petrmanek/libkatherine?env=BADGE=linux&label=build&branch=master
[travis-badge-osx]:     https://badges.herokuapp.com/travis/petrmanek/libkatherine?env=BADGE=osx&label=build&branch=master
[travis-badge-windows]: https://badges.herokuapp.com/travis/petrmanek/libkatherine?env=BADGE=windows&label=build&branch=master

[cbt-doc]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
