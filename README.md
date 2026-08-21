Katherine Control Library
=========================

The Katherine control library contains a working implementation of
UDP-based communication protocol. It may be used to control and receive
data from Timepix3 using Katherine readouts.

This git repository contains 3 libraries in total:

 1. [libkatherine](./c/), a C library,
 2. [libkatherinexx](./cxx/), a C++ header-only wrapper,
 3. [katherine](./python/), a Python wrapper package.

At the present time, the library is **multi-platform**, supporting Linux,
macOS and Windows.

[![CI Status][ci-badge]][ci]


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

| C                             | C++                                   | Python                                           | Purpose                                                                 |
| ----------------------------- | ------------------------------------- | ------------------------------------------------ | ----------------------------------------------------------------------- |
| [kfind](./c/examples/kfind.c) | [kfindxx](./cxx/examples/kfindxx.cpp) | [kfind.py](./python/examples/kfind.py)           | Locate Katherine readouts in given IP address range.                    |
| [krun](./c/examples/krun.c)   | [krunxx](./cxx/examples/krunxx.cpp)   | [krun.py](./python/examples/krun.py)             | Configure & perform data-driven acquisition.                            |
|                               |                                       | [tot_hitmap.py](./python/examples/tot_hitmap.py) | Plot an integrated frame in a pixel matrix from krun output.            |

### Trying Without Hardware

When no readout is available, the [ksim](./tools/ksim/)
daemon (built when `KATHERINE_BUILD_EMULATOR` is enabled) hosts an
emulated readout that the examples can talk to over loopback:

```shell
./ksim --listen 127.0.0.2 &
./krun 127.0.0.2
./kfind 127.0.0.1-5
```

The daemon also offers deterministic seeding, rate shaping and fault
injection; see `ksim --help`.


### Full Documentation

The contents of the C library and the C++ wrapper use in-code Javadoc-style
documentation. If the Doxygen tool is available, the build can produce a
documentation website (see the `KATHERINE_BUILD_DOXYGEN` option in Build
Notes below), in which the two interfaces appear as separate topics.

High-level overview may be found in the Chapter 3 of the thesis.


### Wrappers

For the reasons of redundancy, the Python wrapper is deliberately _not_
documented. Since its programming interface models that of libkatherine,
corresponding functions can be easily identified (usually just by adding the
prefix `katherine_`).


## Build Notes

The project uses CMake 3 build system. It can be configured, built and installed
by standard CMake commands. In case of doubt, check the [CI workflow][ci-yml]
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

| Option                     | Default Value | Meaning                                               |
| -------------------------- | ------------- | ----------------------------------------------------- |
| `KATHERINE_BUILD_CXX`      | `ON`          | Enables building C++ binaries (see requirements)      |
| `KATHERINE_BUILD_PYTHON`   | _detected_    | Enables building Python extension (see requirements)  |
| `KATHERINE_BUILD_EXAMPLES` | `ON`          | Enables building example programs                     |
| `KATHERINE_BUILD_DOXYGEN`  | _detected_    | Enables building HTML documentation                   |
| `KATHERINE_BUILD_TESTS`    | `ON`          | Enables the test suite (run with `ctest`)             |
| `KATHERINE_BUILD_EMULATOR` | `ON`          | Enables the readout emulator (`katherine/emulator.h`) |

The default values of `KATHERINE_BUILD_PYTHON` and `KATHERINE_BUILD_DOXYGEN`
are detected at configuration time. The former is `ON` if a Python 3
interpreter with development headers and the Cython compiler are found; the
latter is `ON` if Doxygen 1.8.13 or newer is found. Both default to `OFF`
otherwise. Setting either option explicitly overrides the detection (forcing
it `ON` without the requirements makes the configuration fail). When
`KATHERINE_BUILD_DOXYGEN` is enabled, the `docs` target builds the
documentation website, and the install step deposits it under
`share/doc/katherine`.

When `KATHERINE_BUILD_TESTS` is enabled, the test suite is registered with
CTest (`ctest -L unit` selects the hardware-free tests). When
`KATHERINE_BUILD_EMULATOR` is enabled, a deterministic emulator of the
readout is compiled into the library and its `katherine/emulator.h` header
is installed, allowing development and testing without hardware.

A summary table of all feature flags and their configured state is printed
at configuration time. The pre-1.0 option names (`BUILD_CXX`, `BUILD_PYTHON`,
`BUILD_EXAMPLES`) are deprecated but still honored; if both the old and the
new name of an option are set, the new name takes precedence.

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

 - Python 3.8 (interpreter and development headers),
 - Cython compiler 3.0 or newer,
 - libkatherine (the C library)

The wrapper builds an extension module whose file name is derived from platform
and Python version. Upon successful build, it is located in the root of the
CMake build directory. While in Linux systems, the file has .so extension
(e.g. `katherine.cpython-314-x86_64-linux-gnu.so`), in Windows the file's
extension is .pyd (e.g. `katherine.cp314-win_amd64.pyd`).

With the default install prefix, the install step copies the module into the
interpreter's site-packages directory, after which `import katherine` works
with no further setup (in \*nix systems the module locates `libkatherine.so`
through its embedded rpath; in Windows `katherine.dll` is installed next to
the module). When a custom `CMAKE_INSTALL_PREFIX` is chosen at configuration
time, the module is kept under that prefix instead (mirroring the
interpreter's site-packages layout, e.g. `<prefix>/lib/python3.14/site-packages`),
and the directory must be added to `PYTHONPATH`. Either behavior can be
overridden by pointing the `KATHERINE_PYTHON_INSTALL_DIR` cache variable
(absolute, or relative to the prefix) at the desired directory, e.g. that of
a virtual environment.

To use the module from the build tree without installing, add the build
directory to `PYTHONPATH`:

```shell
PYTHONPATH=/path/to/build python3 -c 'import katherine'
```


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
 - Felix Lehner, Physikalisch-Technische Bundesanstalt,
 - Stephan Lachnit, Deutsches Elektronen-Synchrotron (DESY),
 - Simon Spannagel, Deutsches Elektronen-Synchrotron (DESY),
 - Stephine Yearley, University of Alberta,
 - Paul Schütze, Deutsches Elektronen-Synchrotron (DESY).


[thesis]: http://hdl.handle.net/20.500.11956/101404

[ci]:       https://github.com/petrmanek/libkatherine/actions/workflows/ci.yml
[ci-badge]: https://github.com/petrmanek/libkatherine/actions/workflows/ci.yml/badge.svg
[ci-yml]:   ./.github/workflows/ci.yml

[cbt-doc]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
