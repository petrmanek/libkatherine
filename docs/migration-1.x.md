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

The enumerators moved with their types, and several of them gained the
library's prefix, which is what made them worth renaming at all: 1.x spelled
them `PHASE_1` and `READOUT_SEQUENTIAL`, short enough that a
data-acquisition program might define them itself, and 2.0 spells them
`KATHERINE_TPX3_PHASE_1` and `KATHERINE_TPX3_READOUT_SEQUENTIAL`. The shim
aliases all of them, which re-pollutes the namespace 2.0 keeps clear --
opting into the shim is opting into that.

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

## 5. The changes with no shim

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

Ten of the eleven 1.x session functions still have declarations, so a
source mentioning them compiles, and they decline at runtime. Code that
*declared* a session -- `katherine_udp_t u;` by value -- does not compile
at all, the type being incomplete by design, which is the limit of what
the shim offers for this family.

`katherine_udp_snprint()` is the eleventh and is not declared. It rendered
a session's endpoints, and an incomplete type has none it could truthfully
print. `katherine_transport_snprint()` is the 2.0 call.

### The measurement word carries an ASIC prefix

`KATHERINE_MD_SIZE` is `KATHERINE_TPX3_MD_SIZE`, six bytes, and
`katherine1.h` aliases the old spelling. A Timepix4 readout sends eight-byte
measurement data, so the constant had to name the family it belongs to
before the tag froze it.

In C++ `katherine::md_size` moved to `katherine::tpx3::md_size` with the
rest of the Timepix3 surface; in Python `MD_SIZE()` is `TPX3_MD_SIZE()`,
which has no alias, Python's shim covering class names rather than module
functions.

### The frame count was respelled

`katherine_config_t::no_frames` is `frame_count`, and
`katherine_set_no_frames()` is `katherine_set_frame_count()`. The field
holds a count, and 1.x named it for the absence of one.
`katherine_emu_t`'s register of the same name moved with it.
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

Neither name is aliased, because neither can be: `bias_id` has no
counterpart, and an alias from the scalar `bias` would compile into an
assignment to the whole array rather than to the supply the source
meant. A 1.x source does not need one -- see the next section, which
gives it the whole 1.x structure instead.

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
`katherine::tpx3::test_pulse_config`; only the member moved.

`katherine1.h` does not alias this one either. The field alias is a macro
over the whole translation unit, and because the header is includable
from C++, an alias would rewrite the `katherine::tpx3::test_pulse_config`
type wherever both headers are in scope.

### The configuration was split in three, and 1.x source keeps the old one

`katherine_config_t` was one flat struct for one Timepix3 chip. It is now
three:

| | |
|---|---|
| `katherine_config_t` | what every readout takes, whatever chip it drives: `chip_type`, `chip_count`, `acq_time`, `frame_count`, `bias[]`, the two triggers |
| `katherine_tpx3_config_t` | that as its first member `common`, plus Timepix3's readout-wide settings, plus `chips[]` |
| `katherine_tpx3_chip_config_t` | one per chip: `polarity`, `gray_disable`, `dacs`, `pixels` |

`readout_mode`, `px_mode` and `fast_vco_enable` were arguments to
`katherine_acquisition_begin()` and are now fields of the Timepix3
configuration, so `begin()` takes three fewer:

```c
katherine_acquisition_begin(&acq, config, decode_data);
```

`katherine_configure()` is `katherine_tpx3_configure()`, and it sends
every chip the configuration declares rather than only chip 0.

Because `chips[]` is a flexible array, the caller allocates:

```c
katherine_tpx3_config_t *c = calloc(1, sizeof(*c) + n * sizeof(c->chips[0]));
```

`calloc`, not `malloc`: zero is a safe value for every field here, so a
configuration only partly filled in still selects
`KATHERINE_POLARITY_HOLES` and an empty matrix rather than whatever the
allocator last held.

**1.x source does not have to move.** No macro can alias one struct into
three, so `katherine1.h` declares `katherine1_config_t` with the whole
1.x layout, shadows the `katherine_config_t` name with it, and
translates in `katherine1_configure()` and
`katherine1_acquisition_begin()`. An unmodified 1.x source -- stack
declaration, `memset`, field assignment, `bias_id` and scalar `bias`
included -- still compiles and still means what it meant. It costs one
`malloc` and a 64 KiB matrix copy per configure, on a path that already
uploads 64 KiB.

`polarity_holes` is deliberately **absent** from `katherine1_config_t`.
Its sense inverted in 2.0, so a field that shimmed transparently would
bias an assembly for the carrier it does not collect and destroy the
chip. A missing member is a compile error naming itself, which is the
only safe translation there is.

### The decoder's own state went behind a reserve

`katherine_acquisition_t` still exposes its state, counters and handlers.
What it no longer exposes is what the decoder keeps between hits:
`last_toa_offset`, `toa_coarse_tick_to_fine_shift`, `phase_count` and
`phase_offsets[]` now live in an opaque `priv` member, and
`katherine_device_t`'s two `udp_*` members do the same.

`phase_correction` **stays readable** -- it is where a phase-correction
request is answered, which is interface rather than bookkeeping.

A 1.x source that read any of the others was reading the decoder's
workings, and there is no alias for it; the compile error is the point.
What such a caller usually wanted is available without them:

| was | use |
|---|---|
| `acq.toa_coarse_tick_to_fine_shift` | `katherine_tpx3_toa_coarse_tick_to_fine_shift(freq)`, with the frequency you configured |
| `acq.phase_count` | `katherine_tpx3_actual_phases(freq, phase)` |
| `acq.phase_offsets[x]` | `katherine_acquisition_timestamp_phase_offset(&acq, coord)` |
| `dev.udp_data_port` | nothing public. It was never a setting, only a record of one. |

The reserve is why 2.1 can add Timepix4 without another SONAME: a new
ASIC changes what is kept inside it and the assertion that it fits,
rather than the layout of a structure callers embed.

**One structure also stopped changing size with the build.**
`sizeof(katherine_device_t)` was 152 bytes with UDP compiled in and 144
without, because those two members sat behind `#ifdef`. It is now the
same either way, so a consumer built against one feature set and linked
against another no longer disagrees about where the fields are.

### C++ throws one type, and it is the one with the code

`katherine::error` carries `code()` now. There used to be a base with
no `code()` and a `katherine::system_error` deriving from it, and since
nothing ever threw the base, the only thing the split achieved was that
the natural

```cpp
} catch (const katherine::error& err) {
    std::cerr << err.code();
}
```

did not compile. `katherine::system_error` remains as a deprecated
alias, so existing `catch` clauses keep working.

### A chip index is the first argument, and never optional

Python's chip-addressed methods took `chip_index=0` last; C++ offered a
chip-0 overload beside each indexed one. Both are gone: the index comes
first and must be passed.

```python
dev.get_chip_id()                 ->  dev.get_chip_id(0)
dev.set_dacs(dacs)                ->  dev.set_dacs(0, dacs)
dev.set_sensor_register(r, v)     ->  dev.set_sensor_register(0, r, v)
```

```cpp
dev.chip_id()                     ->  dev.chip_id(0)
dev.set_all_pixel_config(px)      ->  dev.set_all_pixel_config(0, px)
```

A call that did not name a chip read identically whether its author had
thought about the question or not, which on a two-chip readout is a bug
waiting to be written.

### Python objects close like files

`Device` and `Acquisition` follow the contract `io.IOBase` sets:
`close()` (idempotent), a `closed` property, `with` support, and
`ValueError` -- not one of the `Error` subclasses -- on use after
close. An object collected unclosed raises `ResourceWarning`, because
an open readout holds a claim no other process on the machine can take.

`Device.close()` refuses while an `Acquisition` is open on it. Nothing
cascades; close the acquisition first, or let `with` nest them.

### A trigger channel is a source, and C will not tell you

`katherine_trigger_t::channel` was a `char` and is now
`katherine_trigger_source_t`.

```c
/* 1.x */  trigger.channel = 1;
/* 2.0 */  trigger.channel = KATHERINE_TRIGGER_SOURCE_GPIO_0;
```

The values are unchanged -- zero is the pulse generator's internal
channel and one upward are GPIO 0 to 7 -- so a program that keeps
assigning integers behaves exactly as it did. **In C it also still
compiles**, silently, because an `int` converts to an enumeration; in
C++ the same line stops compiling, which is the only warning either
language gives. Grep for `.channel` rather than waiting for one.

Python is the exception that does tell you, and only sometimes:
`Trigger.channel` is a `TriggerSource` now, so a channel of 0 to 8
reads back as an enumerator and anything else is a `ValueError` where
1.x stored it and C still masks it.

### Two smaller signature changes

`katherine_set_acquisition_triggers()`'s second argument is
`stop_trigger`, matching the struct field and
`katherine_acquisition_stop()`. `katherine_get_tdc_counts()` fills a
`katherine_tdc_counts_t` rather than a bare
`uint32_t[KATHERINE_MAX_TDC_CHANNEL_COUNT]`; the counts are at
`.channel[]`.

### C++ gathers the Timepix3 names under `katherine::tpx3`

`katherinexx` has always tracked the C names rather than offering its own,
so a caller following the C sections above is already following most of the
C++ one. The exception is the namespace. The C surface spells "this is
Timepix3's" with a `tpx3_` prefix; C++ spells it with a namespace, and 2.0
is where that namespace is finished.

Six names move, and the six acquisition tags with them:

| 1.x | 2.0 |
|---|---|
| `katherine::config` | `katherine::tpx3::config` |
| `katherine::px_config` | `katherine::tpx3::px_config` |
| `katherine::bmc`, `katherine::bpc` | `katherine::tpx3::bmc`, `katherine::tpx3::bpc` |
| `katherine::test_pulse_config` | `katherine::tpx3::test_pulse_config` |
| `katherine::phase_correction` | `katherine::tpx3::phase_correction` |
| `katherine::acq::toa_tot` and its five siblings | `katherine::tpx3::acq::toa_tot` |
| `katherine::load_bmc_file()` and the three other loaders | `katherine::tpx3::load_bmc_file()` |

What stays in `katherine::` is what a readout does whatever chip it
carries: `device`, `base_acquisition` and `acquisition<>`, `acq_state`,
`frame_info`, `trigger`, `polarity`, the GPIO, timing-unit and
pulse-generator types, and the error types.

One C++ signature changes with the namespace, and it is the last of the
three shapes `katherine_tpx3_dac_to_si()` had. C's optional out-parameter
became a reference the caller had to declare first; it is now a returned
pair, as `tpx3::timestamp_seconds` and `tpx3::toa_ftoa` already were:

```cpp
katherine_dac_unit_t unit = KATHERINE_DAC_UNIT_VOLT;      // 1.x
double si = katherine::dac_to_si(dac, value, unit);

const auto si = katherine::tpx3::dac_to_si(dac, value);   // 2.0
//    si.value, si.unit
```

There is no C++ shim and will not be one -- an alias would have to be a
`using`, and a `using` in `katherine::` is exactly the namespace pollution
the move exists to end. Every call site fails to compile and the compiler
names it, which for a mechanical rename is the cheapest migration there is.
A `namespace tpx3 = katherine::tpx3;` in your own code shortens what is
left.

### Python renamed fourteen classes, and thirteen of them still answer

The Python module follows the C renames.

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
