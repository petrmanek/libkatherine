# Moving from libkatherine 1.x to 2.0

## The short answer

Add one include and your 1.x source compiles against 2.0:

```c
#include <katherine/katherine.h>
#include <katherine/katherine1.h>   /* after katherine.h, always */
```

`katherine1.h` renames every 1.x spelling onto its 2.0 counterpart and
translates return values back to the 1.x convention. It exports no symbols
of its own -- every wrapper is `static inline` -- so nothing about linking
changes. It is removed in 3.0, and every wrapper carries a deprecation
attribute, so the compiler tells you which call sites are still on it.

Four things it deliberately does **not** fix. Each is a compile error until
you deal with it, which is the point:

- `katherine_config_t::polarity_holes`
- the `katherine_udp_*` session API
- anything reading `katherine_acquisition_t` fields that moved
- C++ and Python callers, who have no shim at all

They are covered below.

---

## 1. Return values

Every function that could fail returned `0` or an `<errno.h>` value in 1.x.
In 2.0 it returns `KATHERINE_E_OK` or a `katherine_error_t` enumerator, and
the codes say what went wrong in this library's own terms rather than in
whatever the failing system call happened to set.

```c
/* 1.x */
int res = katherine_get_chip_id(&dev, chip_id);
if (res != 0) fprintf(stderr, "%s\n", strerror(res));

/* 2.0 */
katherine_error_t res = katherine_get_chip_id(&dev, 0, chip_id);
if (res != KATHERINE_E_OK) fprintf(stderr, "%s\n", katherine_strerror(res));
```

The shim keeps the 1.x convention for 1.x spellings, so you can convert one
call at a time. What you lose by staying on it is the resolution: 2.0 has a
code for "the readout carries no such chip", for "not valid in this state",
for "this readout cannot do that", and 1.x had nowhere to put any of them.
They collapse onto `EIO` through the shim.

Where a call failed at the OS level, the OS code is still available:
`katherine_transport_get_last_os_error()` on the device's control link or
the acquisition's data link.

## 2. Timepix3 names carry a `tpx3_` stem

Everything whose meaning is Timepix3's -- its DACs, its pixel matrix, its
registers, its clock model, its hit formats -- moved under
`katherine_tpx3_`. The library will drive other ASICs, and a name like
`katherine_dacs_t` cannot mean two different register sets at once.

| 1.x | 2.0 |
|---|---|
| `katherine_set_all_pixel_config` | `katherine_tpx3_set_all_pixel_config` |
| `katherine_set_acq_mode` | `katherine_tpx3_set_acq_mode` |
| `katherine_set_seq_readout_start` | `katherine_tpx3_set_seq_readout_start` |
| `katherine_set_sensor_register` | `katherine_tpx3_set_sensor_register` |
| `katherine_update_sensor_registers` | `katherine_tpx3_update_sensor_registers` |
| `katherine_output_block_config_update` | `katherine_tpx3_output_block_config_update` |
| `katherine_timer_set` | `katherine_tpx3_timer_set` |
| `katherine_set_dacs` | `katherine_tpx3_set_dacs` |
| `katherine_set_test_pulses` | `katherine_tpx3_set_test_pulses` |
| `katherine_perform_digital_test` | `katherine_tpx3_perform_digital_test` |
| `katherine_px_config_load_b{m,p}c_{file,data}` | `katherine_tpx3_px_config_load_...` |
| `katherine_dacs_t`, `katherine_coord_t`, `katherine_px_*_t` | `katherine_tpx3_...` |
| `katherine_readout_type_t`, `katherine_acquisition_mode_t` | `katherine_tpx3_readout_mode_t`, `katherine_tpx3_px_mode_t` |
| `katherine_phase_t`, `katherine_freq_t` | `katherine_tpx3_phase_t`, `katherine_tpx3_freq_t` |

One rename is not a stem change: `katherine_acquisition_setup()` is
`katherine_set_acquisition_triggers()`, which is what it always did.

The enumerators moved with their types: `KATHERINE_PHASE_1` is
`KATHERINE_TPX3_PHASE_1`, `KATHERINE_READOUT_SEQUENTIAL` is
`KATHERINE_TPX3_READOUT_SEQUENTIAL`, and so on. The shim aliases all of
them, which re-pollutes the namespace 2.0 keeps clear -- opting into the
shim is opting into that.

## 3. Per-chip calls name their chip

A second-generation readout drives up to eight chips. Every call that
addresses one now takes the index second, after the device:

```c
katherine_tpx3_set_dacs(&dev, chip_index, &dacs);
katherine_tpx3_set_all_pixel_config(&dev, chip_index, &px);
katherine_get_chip_id(&dev, chip_index, chip_id);
katherine_get_sensor_temperature(&dev, chip_index, &celsius);
```

A chip the readout does not report is refused with `KATHERINE_E_BAD_CHIP`
before anything reaches the wire -- including chip 0 on a device that has
been neither enumerated nor declared, which carries no chips at all.

The shim passes 0, which is what 1.x always addressed.

`katherine_configure()` still configures chip 0, and says so. Configuring a
multi-chip readout is a call per chip for now.

## 4. Opening a device

```c
/* 1.x */
katherine_device_init(&dev, address);

/* 2.0 */
katherine_device_init(&dev, address, 0);
```

2.0 does three things on open where 1.x did two: it enumerates the readout,
moves the session to a private pair of UDP ports, and tells the readout
which interface drives it. The third is what a second-generation readout
needs before it will start an acquisition over UDP at all.

`katherine_device_flags_t` suppresses each one. The shim passes
`DONT_CHANGE_UDP_PORTS | DONT_SELECT_INTERFACE`, so a program compiled
through it keeps the fixed ports it may rely on and behaves as it did.

An address now carries a scheme: `udp://192.168.1.142`, `usb://auto`. A
bare address is still read as UDP.

## 5. The four things with no shim

### `polarity_holes` is gone, and its replacement means the opposite

```c
/* 1.x */  config.polarity_holes = false;        /* collected electrons */
/* 2.0 */  config.polarity = KATHERINE_POLARITY_ELECTRONS;
```

`katherine_polarity_t` takes the sense of the chip's own GeneralConfig bit,
and `polarity = 0` is `KATHERINE_POLARITY_HOLES` -- the opposite of what
`polarity_holes = 0` selected.

An alias here would compile and bias the assembly for the carrier it does
not collect, which destroys the chip. So there is none: every site is a
compile error, and every one has to be read and converted by hand. **Check
each against your hardware, not against this table.**

### The UDP session type went private

1.x let a caller open a `katherine_udp_t` and drive it. 2.0 names a readout
by address and opens it with `katherine_device_init()`; the session lives
behind a transport the device owns, and the same code drives a USB readout
with no change at the call site.

The 1.x functions still have declarations, so a source mentioning them
compiles, and they decline at runtime. Code that declared a session has to
be ported rather than recompiled.

### Some acquisition fields moved

`katherine_acquisition_t` still exposes its state, counters and handlers.
Fields marked `Internal:` in `acquisition.h` are not part of the interface
and may move again.

### C++ and Python have no shim

`katherinexx` and the Python module follow the C renames, and the shim is a
C header. Both wrappers are source-compatible in the places where the C API
did not move, and each has gained surface in 2.0 rather than lost it; see
`README.md` for what they bind.

Python's exceptions now carry the code:

```python
try:
    dev.get_chip_id()
except katherine.Error as e:
    print(e.code)        # katherine.ErrorCode
```

`TimeoutError`, `ValueError` and `MemoryError` still work -- the classes
raised for those three codes derive from both.

## 6. Linking

The SONAME is `libkatherine.so.2`. A 1.x binary does not load against it and
is not meant to: the return convention change is an ABI break even where a
signature did not move. Recompile.

`pkg-config --cflags --libs katherine` is unchanged otherwise.
