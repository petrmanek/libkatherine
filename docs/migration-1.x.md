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

### The frame count was respelled

`katherine_config_t::no_frames` is `frame_count`, and
`katherine_set_no_frames()` is `katherine_set_frame_count()`. The field
holds a count, and 1.x named it for the absence of one.
`katherine_emulator_t`'s register of the same name moved with it.
The field and the setter's parameter are `uint32_t`, where 1.x declared
both `int`. `katherine1.h` aliases both old spellings and
`katherine_set_no_frames()` keeps the `int` parameter it had, so a 1.x
source that sets either keeps compiling -- but a negative count, which
1.x gave no meaning to, now converts to a large positive one instead of
reaching the readout as a negative number.

### One bias supply became every bias supply

`katherine_config_t::bias_id` and `bias` are gone. In their place is

```c
float bias[KATHERINE_CONFIG_MAX_BIAS_SUPPLIES];
```

addressed from 0 exactly as `katherine_set_bias()` addresses a supply,
and `katherine_configure()` now sends **every** supply the readout
reports rather than the one `bias_id` chose. The conversion is
mechanical -- the index becomes the subscript:

```c
config.bias_id = 1;        config.bias[1] = 230.0f;
config.bias    = 230.0f;
```

Note the change in effect, not only in spelling: an entry left at zero
drives that supply to zero, where 1.x left every supply but one alone.
On a readout with a single supply nothing changes.

`katherine1.h` aliases neither. `bias_id` has no counterpart to alias
to, and an alias from the scalar `bias` would compile into an assignment
to the whole array instead of to the supply the source meant -- so both
are compile errors at every site, which is the only safe outcome.

### The pixel-clock settings say which clock

`katherine_config_t::phase`, `freq` and `correct_phase` are
`clock_phase`, `clock_frequency` and `correct_clock_phase`. The
enumerators are untouched: `KATHERINE_TPX3_PHASE_1` and
`KATHERINE_TPX3_FREQ_40_MHZ` keep their names and values.

`katherine1.h` aliases `correct_phase` only. It cannot alias `phase` or
`freq`, because a field alias is a macro over a whole translation unit:
`phase` would also rewrite `katherine_tpx3_test_pulse_config_t::phase`,
which did not move, and `freq` would rewrite whatever else the consumer
calls by that name. Those two convert by hand, at sites the compiler
names.

### The test pulses are the pulses, not a configuration

`katherine_config_t::test_pulse_config` is `test_pulses`, which is what
the device call that sends it has always been called
(`katherine_tpx3_set_test_pulses()`). The struct type keeps its name, in
C as `katherine_tpx3_test_pulse_config_t` and in C++ as
`katherine::test_pulse_config`; only the member moved.

`katherine1.h` does not alias this one either. The field alias is a macro
over the whole translation unit, and because the header is includable
from C++, an alias would rewrite the `katherine::test_pulse_config` type
wherever both headers are in scope.

### Some acquisition fields moved

`katherine_acquisition_t` still exposes its state, counters and handlers.
Fields marked `Internal:` in `acquisition.h` are not part of the interface
and may move again.

### Python renamed fourteen classes, and thirteen of them still answer

`katherinexx` and the Python module follow the C renames. C++ is the easy
half: it has always tracked the C names rather than offering its own, so a
caller following the C sections above is already following the C++ one.

**Python has a shim of its own**, matching what `katherine/katherine1.h`
does for C and carrying the same promise -- deprecated in 2.0, removed in
3.0. Thirteen of the fourteen renamed classes resolve to their 2.0
spelling and warn once per call site:

```
DeprecationWarning: katherine.Dacs is deprecated since 2.0 and becomes
katherine.Tpx3Dacs; see docs/migration-1.x.md
```

The object you get back **is** the 2.0 class, not a wrapper around it, so
`katherine.Dacs is katherine.Tpx3Dacs` and every `isinstance` check holds.
Nothing else about the script has to change for it to keep running.

Python shows `DeprecationWarning` by default only for code in `__main__`,
which is where an analysis script lives. A program that reaches these
names from inside a package of its own will not see the warning without
`-W default`, and should consult the table rather than wait to be told.

| 1.x | 2.0 |
|---|---|
| `katherine.Dacs` | `katherine.Tpx3Dacs` |
| `katherine.PxConfig` | `katherine.Tpx3PxConfig` |
| `katherine.TestPulseConfig` | `katherine.Tpx3TestPulseConfig` |
| `katherine.PxToaTot` | `katherine.Tpx3PxToaTot` |
| `katherine.PxToaOnly` | `katherine.Tpx3PxToaOnly` |
| `katherine.PxFastToaTot` | `katherine.Tpx3PxFastToaTot` |
| `katherine.PxFastToaOnly` | `katherine.Tpx3PxFastToaOnly` |
| `katherine.PxEventItot` | `katherine.Tpx3PxEventCountItot` |
| `katherine.PxFastEventItot` | `katherine.Tpx3PxFastEventCountItot` |
| `katherine.Freq` | `katherine.Tpx3Freq` |
| `katherine.Phase` | `katherine.Tpx3Phase` |
| `katherine.AcquisitionMode` | `katherine.Tpx3PxMode` |
| `katherine.ReadoutType` | `katherine.Tpx3ReadoutMode` |
| `katherine.Udp` | nothing -- the session type went private, as above |

`katherine.Udp` is the fourteenth and the only one with no replacement, so
it raises rather than resolving, naming `katherine.Device('udp://...')` as
what to open a readout with instead. A name that never existed in either
version raises the ordinary `AttributeError`.

The prefix is the same one the C and C++ sides carry, and it means the same
thing: the class describes something Timepix3-specific, and a Timepix2 or
Timepix4 equivalent would not be interchangeable with it.

One enumeration **member** moved with them, and it is the only one:

```python
# 1.x
katherine.AcquisitionMode.EVENT_ITOT
# 2.0
katherine.Tpx3PxMode.EVENT_COUNT_ITOT
```

That mode's four-bit field counts hits rather than measuring a current, and
the two `PxEventItot` hit classes gain `Count` for the same reason.
`Tpx3Freq` and `Tpx3Phase` kept every member spelling they had, so
`Freq.FREQ_40` becomes `Tpx3Freq.FREQ_40` and nothing else.

Everything else the module binds kept its name, and 2.0 adds to it rather
than removing further; see `README.md`.

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
