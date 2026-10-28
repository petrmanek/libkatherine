# Changelog

Notable changes per release. The full annotated notes accompany each tag
as a GitHub/GitLab release; this file is the offline summary.

## Unreleased (2.0.0-dev)

The 2.0 series is a clean break of the C API: the shared library becomes
`libkatherine.so.2`, return codes move from positive errno values to the
library's own error domain, and per-chip addressing enters the public
surface. Source compatibility for the documented 1.x API is kept through
the deprecated `katherine/katherine1.h` shim header, planned for removal
in 3.0. The 1.x line ends with 1.1.0; no further 1.x releases are
planned.

The public identifiers are renamed once, here, and then frozen. Everything
2.0 added carries a `KATHERINE_` prefix and everything inherited from 1.x
did not, so `PHASE_1`, `FREQ_40`, `ACQUISITION_RUNNING` and
`READOUT_SEQUENTIAL` -- short enough that a data-acquisition program might
define them itself -- become `KATHERINE_TPX3_PHASE_1`,
`KATHERINE_TPX3_FREQ_40_MHZ`, `KATHERINE_ACQUISITION_STATE_RUNNING` and
`KATHERINE_TPX3_READOUT_SEQUENTIAL`. The names are the final ones, ASIC
prefix included: the clock, pixel-mode and register enumerations are
Timepix3's and take `tpx3_`, while the acquisition lifecycle is generic and
stays bare. `katherine_asic_t` becomes `katherine_chip_type_t`, an ASIC
being the technology a chip is built in while the chip is what a readout
carries and addresses. The stringifiers named after the renamed types
follow. No value moved; every 1.x spelling is aliased in
`katherine/katherine1.h`.

**The C++ wrapper finishes its `katherine::tpx3` namespace.** What a
Timepix3 readout measures with is already there -- the DACs, the monitors,
the clock enumerations, the coordinate, the ToA helpers. The six names that
were not have joined them: `config`, `px_config`, `bmc`, `bpc`,
`test_pulse_config` and `phase_correction`, along with the six acquisition
tags of `katherine::acq`, which become `katherine::tpx3::acq`. What stays in
`katherine::` is what a readout does whatever chip it carries -- the device,
the acquisition, the GPIO, the timing unit, the pulse generator, the error.
C++ has no shim header, so this is a source break for a C++ consumer: five
type spellings and one namespace qualifier, and the compiler names every
one.

One field is renamed without an alias because the two spellings disagree
about what a value means. `katherine_config_t`'s
`bool polarity_holes` becomes `katherine_polarity_t polarity`, with
`KATHERINE_POLARITY_HOLES` zero and `KATHERINE_POLARITY_ELECTRONS` one --
the Polarity[0] bit of GeneralConfig itself, so the setting can be compared
against a register read-back without translation.

Zero had to change meaning for this. Configurations are routinely zeroed
before their fields are filled, and biasing an assembly for the carrier it
does not collect destroys the chip, so the value a forgotten field takes
must be the conservative one; under `polarity_holes` it was not, zero
meaning "not holes" and therefore electrons. It now means holes. The
encoder's golden vectors assert this directly: an all-zero configuration
must produce `0x58`, and reading `0x59` again means the safe default has
been lost.

That is also why the shim leaves this one field out. A field can be aliased
there -- `chip_detected` is -- but only where both spellings agree on what a
value means, and these do not: `polarity_holes = false` selected electrons
while `polarity = false` selects holes. A 1.x source under an alias would
keep compiling and bias the wrong way, so instead it fails to compile at
every site, and each has to be read and converted by hand. The wire encoding
is unchanged for anyone who set the field explicitly.

The shutter's triggers are restructured, which breaks source as well.
`katherine_trigger_t::use_falling_edge` becomes `katherine_trigger_edge_t
edge`, and the timing unit's channel configuration takes the same type.
Zero still means a rising edge, so a zeroed structure behaves exactly as it
did and only the spelling has to change.

`katherine_trigger_t::enabled` becomes `use_trigger` in the same structure.
The field never meant "is this trigger switched on": it selects whether that
shutter edge comes from the trigger input or from the timer, which is what the
firmware calls Timer/Trigger. Zero still means the timer, so a zeroed structure
is unchanged in behaviour and only the spelling moves.

It is not aliased in the shim, and for a different reason than `polarity_holes`:
`enabled` is a common enough word that an object-like macro would rewrite
unrelated identifiers in the caller's own code. `katherine/katherine1.h` states
that rule, and this is the case it was written for. A 1.x source naming the
field fails to compile, which is the intended outcome.

The same structure gains `delayed` and `delay_seconds`, which is where a
trigger's delay now lives. `katherine_config_t::delayed_start` and
`katherine_set_shutter_delays()` go away with it, and
`katherine_set_acquisition_triggers()` drops its `delayed_start` argument.
This reaches the stop delay, which nothing reached before: the second
generation's trigger word carries a delay flag in each of its two bytes,
and only the start one was ever set. The 1.x spelling keeps its four
arguments, the shim copying the start trigger and setting the flag from
its own.

Command responses are now correlated with the request that provoked them,
instead of the next datagram to arrive being taken as the answer: a
session flushes what an earlier exchange left behind before it sends,
steps over responses belonging to no request of its own — counting them in
`katherine_transport_t::stray_command_responses`, and giving up with
`KATHERINE_E_STRAY` rather than waiting forever — and reports a response
of the wrong length as `KATHERINE_E_BAD_CRD`. The identifiers the readout
firmware substitutes for a request's own operation code are accepted by
default; `katherine_transport_set_strict_ack()` turns that tolerance off
once a peer has been shown not to need it.

Acquisition modes now actually reach the sensor. The acquisition-mode
command is a GeneralConfig accessor — the readout merges the pixel mode
and the fast-oscillator flag into its own register image — and the image
only reaches the chip once the sensor registers are flushed. libkatherine
never flushed, and put the oscillator flag one byte off, so every
acquisition ran in whichever mode the preceding configuration had left
behind: ToA+ToT with the fast oscillator on, whatever was requested.
Reading the register back on a Gen1 readout now shows the requested mode
and flag in all six combinations.

The Event+iToT hit counter moves to the variant that actually carries it.
Timepix3 manual Figure 1 gives bits [3:0] of an Event+iToT word as dummy with
the fast oscillator on and as the pixel hit counter with it off -- the one mode
whose fast variant carries less than its slow one, there being no fine ToA to
report. `katherine_tpx3_px_f_event_count_itot_t` exposed a `hit_count` that
read the dummy field, and `katherine_tpx3_px_event_count_itot_t` omitted one
and discarded a real counter. The field swaps between them, so both structs
change size.

Measured on a Gen1 readout rather than taken from the figure. With the
oscillator on, 3012 pixels at high occupancy read zero throughout while the
event counter saturated. With it off, a patch given N test pulses reads exactly
N for N up to 14 and saturates at 14 -- Table 4's limit for this counter, and
what distinguishes it from the fine-ToA field, which saturates at 15.

The same measurement settles a longer-standing question for two fields: the
readout does decode the chip's counter encoding, since `event_count` reads
exactly the pulse count. `tot` and `integral_tot` have not been measured and
are still documented as raw counter states.

Per-pixel threshold adjustments are now in the chip's DAC-value order.
`katherine_px_config_set_loc_thl()` wrote the nibble unreversed, but Timepix3
manual Table 23 stores `Thr[3:0]` with its least significant bit in `PCR[4]`
and its most significant in `PCR[1]`, so an argument of 1 programmed a trim of
8 and only 0, 6, 9 and 15 meant what they said. The getter had the same skew.
The BPC loader always had the order right, so the two halves of this library
disagreed with each other. Callers who compensated for the old behaviour must
stop doing so; those who took the documented meaning at face value are
correct for the first time.

`katherine_comm_status_t::data_rate` is now scaled correctly and
documented as Mb/s. The register field counts megabytes per second, so it
scales by eight, not by the five the readout manual specifies: a Gen1
readout reporting 160 has two links active at 640 Mb/s each, which is the
1280 Mb/s the vendor tool displays and not 800.

**Per-chip addressing reaches the configuration calls.** A sweep of every
function sending an opcode the firmware reads a chip index from found ten
without one; only the DAC setters and the monitor reads ever placed it, and
every other per-chip call addressed chip 0 whatever the readout carried. The
sensor-register write and the three hardware commands that latch it, the
pixel-matrix upload, the chip identifier, the sensor temperature and the
digital test now take the index as their second argument, and refuse a chip
the readout does not report before anything is sent. Where the index goes in
the datagram is the firmware's answer rather than a guess: byte 5 for the
register write, byte 1 for the hardware-command dispatch, byte 0 for the chip
reads. `katherine_tpx3_configure()` sends every chip the configuration
carries, looping the chip array the three-struct split gave it.

Two neighbours deliberately keep no index, both confirmed against the readout
firmware. `katherine_tpx3_set_acq_mode()` sends an opcode with no chip-index
slot at all, and the readout's own decoder takes its mode from chip 0's
GeneralConfig for the whole data path, so readout-wide is the faithful model
rather than a gap. `katherine_tpx3_start_readout()` sends a command
the firmware broadcasts by construction: it spends its first byte on the
readout type and writes the chip-select register's broadcast value
unconditionally, so a sequential readout cannot be started on one chip of
several.

**The C++ wrapper keeps the error code.** Forty-six call sites stored a
`katherine_error_t` in an `int`, compared it to zero and threw it away. They
are one `katherine::detail::check()` now, and `katherine::system_error` takes
the enumerator and keeps it as `code()`, so a C++ caller can tell
`KATHERINE_E_STATE` from `KATHERINE_E_INVAL` on the same call as a C caller
can.

**Python's exceptions carry the code too.** Every failure is a
`katherine.Error`, a `RuntimeError` carrying `.code` -- a
`katherine.ErrorCode`, the same enumeration the C library returns.
`katherine.Timeout`, `katherine.InvalidArgument` and `katherine.OutOfMemory`
derive from `TimeoutError`, `ValueError` and `MemoryError` as well, so code
written against those keeps working. One consequence worth knowing: a timeout
is now a `RuntimeError` as well as a `TimeoutError`, so a caller whose
`except RuntimeError` clause comes first takes that branch instead.

**A device and an acquisition cannot be copied.** `katherine::device` and
`katherine::base_acquisition` delete their copy operations, which suppresses
the moves with them. Copying a device shallow-copied sockets the original
still owned and finalised them twice; moving an acquisition left the
readout's data arriving at the address the object used to occupy, its
constructor having handed `this` to the C callbacks. `katherine::tpx3::config` is
deliberately left copyable: it owns nothing but a `katherine_tpx3_config_t`, and
holding two configurations that differ in one setting is the ordinary use.

**Two renames, both pre-2.0 and both of spelling only.**
`katherine_tdc_source_str()`, `katherine_pulse_source_str()` and
`katherine_pulse_output_str()` become `katherine_str_tdc_source()`,
`katherine_str_pulse_source()` and `katherine_str_pulse_output()`, which is
how the other thirteen renderers in this library are spelled; `tdc.h` and
`pulse.h` are new in 2.0 and neither name has shipped.
`katherine_tpx3_set_sensor_register()` takes `katherine_tpx3_reg_t` where it
took a `char`, three lines below the enumeration that names the registers;
the 1.x shim keeps the `char` it exists to preserve.

**Both wrappers reach the rest of the surface.** Of the 145 exported
functions, C++ reaches 126 and Python 118, against 99 and 104 in 1.x. The
nineteen neither reaches are the emulator's eleven and the transport data
plane's eight, which the device and the acquisition exist to drive; nothing
else is left out. What arrives: the pure helpers a caller previously had to
reimplement
(`katherine_tpx3_actual_phases()`,
`katherine_tpx3_freq_is_fast_vco_supported()` and
`katherine_tpx3_dacs_validate()`), `verify_config()` and `set_token_count()`,
the device flags, `declare()`, `select_interface()` and the device's own
`info`, the three transport accessors a device and an acquisition can now
reach -- `last_os_error()`, `set_timeout()` and `dropped_input()`, which is
what the hundreds of *"see katherine_transport_get_last_os_error()"* lines
had been pointing at -- and five types that used to stream as their numbers.

**Finalising an object whose initializer failed no longer crashes.** Both
`katherine_device_init()` and `katherine_acquisition_init()` could return
before the field their finalizer keys on was written, and the caller's object
is uninitialized until the initializer runs, so finalising afterwards
dereferenced whatever happened to be there. `katherine.Device` on an
over-long address segfaulted the interpreter, Cython running `__dealloc__`
even when `__cinit__` raised. Both initializers now clear the caller's
storage before anything that can fail.

The same shape was found in three more places and closed:
`katherine_udp_init_bound()` left a closed socket's number in the session, so
finalising a failed open closed a descriptor the process had since reused;
and the two transport constructors wrote their operations table only on
success.

**A buffer size within a word of `SIZE_MAX` is refused.** The UDP transport
allocates a word past the size it is asked for, because a decoder reads the
last six-byte item through an eight-byte load. Nothing bounded the size, so
that addition could wrap to zero -- and `realloc(p, 0)` frees `p` and returns
NULL, which the code read as an allocation failure and so kept the freed
pointer for the next finalize to free again. One integer through
`katherine_acquisition_init()` reached it.

**Moving from 1.x is written down.** `docs/migration-1.x.md` covers the
return convention, the `tpx3_` stem, per-chip calls, opening a device, the
changes that have no shim, and linking. Some of those could not have one,
`polarity_holes` foremost: an alias there would compile and bias the sensor
for the carrier it does not collect. The guide also names the fourteen Python
classes a 1.x script meets under new spellings; thirteen resolve through the
module's own fallback with a `DeprecationWarning` naming the replacement, and
the fourteenth is gone with the type it wrapped.

### The configuration is three structs, and a readout's chips are an array

`katherine_config_t` was one flat struct describing one Timepix3 chip, so
a readout carrying two could only be configured by calling the granular
setters per chip after `katherine_configure()` had addressed chip 0. It
is now split by what a setting belongs to:

| | |
|---|---|
| `katherine_config_t` | what any readout takes: `chip_type`, `chip_count`, `acq_time`, `frame_count`, `bias[]`, the two triggers |
| `katherine_tpx3_config_t` | that as its first member `common`, Timepix3's readout-wide settings, and `chips[]` |
| `katherine_tpx3_chip_config_t` | one per chip: `polarity`, `gray_disable`, `dacs`, `pixels` |

`katherine_configure()` is `katherine_tpx3_configure()`. It refuses a
configuration whose `common.chip_type` is not `KATHERINE_CHIP_TPX3`
before reading anything else, and sends the per-chip settings to every
chip the configuration declares.

`chips[]` is a C99 flexible array, so the caller allocates:

```c
katherine_tpx3_config_t *c = calloc(1, sizeof(*c) + n * sizeof(c->chips[0]));
```

`calloc` rather than `malloc`, because zero is a safe value for every
field here — `KATHERINE_POLARITY_HOLES` included.

`readout_mode`, `px_mode` and `fast_vco_enable` were arguments to
`katherine_acquisition_begin()` and are configuration now, so it takes
three fewer. Unmodified 1.x source does not have to move: `katherine1.h`
declares `katherine1_config_t` with the whole 1.x layout, shadows the
name, and translates. `polarity_holes` stays deliberately absent from it.

### The phase correction is Timepix3's, and now says so

`katherine_acquisition_timestamp_phase_offset()` becomes
`katherine_tpx3_timestamp_phase_offset()`, and
`katherine_device_can_correct_timestamp_phase()` becomes
`katherine_tpx3_can_correct_timestamp_phase()`.

Both were the last bare links in a chain that is otherwise entirely
prefixed: the first takes a `katherine_tpx3_coord_t`, returns the offset of
a **Timepix3 double column**, and feeds `katherine_tpx3_timestamp_to_toa_ftoa()`
-- and every other name in `toa.h` already carried the stem. Neither
Timepix2 nor Timepix4 staggers its clock phases, so neither will have an
equivalent to be confused with.

Found by `tools/check_asic_prefix.py` on its first run, which is what that
check exists for. No shim alias: both are 2.0's own, 1.x having exposed a
`phase_offsets[]` array rather than a call, and the migration guide's row
for that array names the new spelling.

### Smaller renames in the same sweep

`no_frames` is `frame_count`, and `uint32_t` where 1.x declared `int`;
`phase`, `freq` and `correct_phase` are `clock_phase`,
`clock_frequency` and `correct_clock_phase`;
`test_pulse_config` is `test_pulses`; `bias_id` and the scalar `bias`
become `bias[KATHERINE_CONFIG_MAX_BIAS_SUPPLIES]`, and
`katherine_tpx3_configure()` drives **every** supply the readout
reports rather than one. `katherine_set_acquisition_triggers()`'s
second parameter is `stop_trigger`, the name of the field it is copied
into -- a parameter only, since no field was called anything else.
`katherine_get_tdc_counts()` fills a `katherine_tdc_counts_t` instead of
a bare array.

### The bindings

A chip index is the first argument of every chip-addressed call and is
never optional: Python's eleven `chip_index=0` defaults are gone and so
are C++'s ten chip-0 overloads. A call that does not name a chip said
nothing about which one it configured, which on a two-chip readout is a
bug waiting to be written.

C++ has one exception type again. `katherine::error` carries `code()`;
`katherine::system_error` is a deprecated alias of it. Catching the base
and reading its code is what a reader writes, and it used not to compile.

`katherine.Config` becomes `katherine.Tpx3Config`, the last binding class
whose C type is `katherine_tpx3_config_t` and whose C++ twin is
`katherine::tpx3::config` to keep a bare name. The old spelling resolves
with a `DeprecationWarning` like every other 1.x name.

Python's `Device` and `Acquisition` follow the contract `io.IOBase`
sets — `close()`, `closed`, `with`, `ValueError` on use after close, and
a `ResourceWarning` when collected unclosed. `Acquisition.__dealloc__`
freed outside its NULL guard, which `close()` would have turned into a
double free.

### The decoder's state is reserved, not exposed

`katherine_acquisition_t` and `katherine_device_t` each carry a
fixed-size opaque `priv` member in place of the fields the library keeps
for itself — the Timepix3 decoder's `phase_offsets[]`,
`last_toa_offset`, `toa_coarse_tick_to_fine_shift` and `phase_count`,
and the device's two `udp_*` records. `phase_correction` stays public;
it is where a phase-correction request is answered.

Reserved inline rather than behind a pointer, so it costs no allocation
and no dereference on a path that runs per hit. The Timepix3 state
measures 272 B against a 384 B reserve. What it buys is 2.1: a second
ASIC changes what is kept inside and the assertion that it fits, rather
than a layout callers embed.

One side effect is worth stating on its own.
`sizeof(katherine_device_t)` used to be 152 bytes with UDP compiled in
and 144 without, because two members sat behind `#ifdef` in a public
structure. It is now 216 either way -- measured in all three transport
shapes -- so a consumer built against one feature set and linked against
another no longer disagrees about where the fields are.

A capability column goes on the **end** of
`katherine_device_derived_info_t`, which is why the supply range sits after
`supported` rather than beside `bias_supply_count` where it reads better:
Track Lab sizes its readout view from this structure, so anything inserted
ahead of a member moves it. `test_layout.c` asserts the ordering rather
than asking for it.

### The transport is no longer a layout a consumer can hold

`katherine_transport_ops_t` is an incomplete type. The public header
forward-declares the struct and keeps the typedef; the operations move
to a header that is not installed, and `katherine_transport_t` keeps its
pointer, a pointer to an incomplete type being all it ever needed. The
vtable's layout leaves the ABI with it, so a further operation in 2.1
costs a consumer nothing.

`katherine_transport_set_buffer_size()` is gone. The data buffer size is
an argument of `katherine_acquisition_init()` instead, and it is a hint
rather than a demand: a link serves the nearest size it can and reports
what it settled on in `katherine_acquisition_t::md_buffer_size`, zero
asks for the link's own size -- which the USB link used to call an error
-- and a size no link can serve is refused at init with
`KATHERINE_E_INVAL`.

### A trigger names its source, and an index starts at zero

`katherine_trigger_t::channel` was a `char` and is now
`katherine_trigger_source_t`, numbered as the readout numbers it:
`KATHERINE_TRIGGER_SOURCE_PULSE_INTERNAL` is zero and GPIO channels 0 to
7 follow it. `katherine_set_acquisition_triggers()`'s second parameter is
`stop_trigger`, agreeing with the field it is copied into and with
`katherine_acquisition_stop()`.

The wrappers follow. C++ takes an enumerator where it used to take an
integer literal, an unscoped enum accepting none, and Python gains
`TriggerSource`, which `Trigger.channel` reads and writes the way it
already does `TriggerEdge` -- so a channel outside the nine is a
`ValueError` there, where C masks it.

Two conventions that were true and unwritten are now stated once, in the
C API's own documentation rather than per call. Every index this
interface exposes counts from zero, whatever the label on a readout's
case says. And every pointer the interface writes a result through
accepts `NULL`, which issues the command and discards the answer, while
every pointer it reads from does not; the one in-out pointer, the count
the discovery calls take, refuses `NULL` with `KATHERINE_E_INVAL`
because it carries the buffer's capacity in.

### A readout can be found rather than typed in

`katherine_device_discover_udp()` sweeps an address range and
`katherine_device_discover_usb()` walks the bus; both fill an array of
`katherine_discovered_t` and write back how many answered. What each one
carries is the address the readout answered from, **spelled with its
transport** -- `udp://192.168.1.42`, `usb://4-1` -- and so ready to hand
to `katherine_device_init()` without assembling anything.

The structure is common to both links, because what differs is how a
transport goes looking and not what it finds. Its other field is `legacy`,
true where the firmware ignored the reply port and answered on the
standard one, which is a property of the firmware rather than of the
generation.

`KATHERINE_DISCOVER_MIN_PORT` is the floor on that reply port. The
firmware reads it out of the request's first two bytes and falls back to
its own default below this, which would send every answer to the port a
running session is already using.

The two links differ in one way worth knowing: the USB walk opens
nothing. It reads descriptors and topology only, so it answers before a
udev rule has granted access to the device, where the UDP sweep has to put
a datagram on the wire and wait.

`kfind` is the example: with no argument it scans `192.168.1.100-200`, and
each of the four octets may be a number or a `lo-hi` range. C++ has
`katherine::discover()` and `katherine::discover_usb()` returning a
`std::vector<katherine::discovered>`; Python has `discover_udp()` and
`discover_usb()` returning `(address, legacy)` pairs.

### The examples name the chip they are talking to

`kinfo`, `ktemp`, `krun` and `kdac` address every chip a readout
reports rather than chip 0. `kdac`'s `--chip` is required by `set`,
`scan` and `monitor` instead of defaulting to zero, and it means an
index and nothing else: the chip type `list` takes is `--asic <type>`
now, and each command refuses the flag it does not take.

### The measurement buffer is one size on every link

`katherine_acquisition_init()`'s `md_buffer_size` names the whole buffer
whatever the link does with it. A UDP session reads into one buffer of
that size, as before; a USB ring now divides it across the reads it keeps
outstanding, rounded down to whole frames, where it used to give each of
them that much. A size chosen for UDP therefore asked a 64-slot ring for
64 times it and the open failed. `katherine_acquisition_t::md_buffer_size`
holds what the ring settled on altogether.

A USB ring is held by the kernel while its reads are outstanding, so its
ceiling is usbfs's rather than the host's memory --
`/sys/module/usbcore/parameters/usbfs_memory_mb`, 16 MiB by default. A
larger ring is refused with `KATHERINE_E_NOMEM` however much memory the
host has, and the examples' 200 MiB is such a size.

### Timing counts survive a readout that splits its answers

`katherine_get_tdc_counts()` tolerates the short reads firmware 5
interleaves when it answers over USB, where it used to fail the call with
`KATHERINE_E_BAD_CRD` and hand back some counts and some of the caller's
own values. The answers themselves arrive whole, so they are read and the
fragments dropped; a readout that sends nothing but fragments is reported
as `KATHERINE_E_UNSUPPORTED` once the discard budget runs out. A firmware
that frames its answers correctly never enters the tolerance, so nothing
has to be configured to prefer it.

Nothing is written to the caller's structure unless the whole sequence
arrived. The earlier behaviour -- counts not received left unchanged, so
that zeroing the array first showed how far the sequence got -- is gone,
a part of an answer not being distinguishable from the caller's own
values.

### A call on something that was never opened answers instead of crashing

`katherine_device_init()` zeroes the device before the first step that can
fail, so a caller who ignores its error and carries on holds one with no
link. Twenty-one public calls used to dereference it; ten more joined them
once `katherine_device_declare()` had been used on such a device, because
the guard they relied on establishes enumeration rather than an open link.
All of them now answer `KATHERINE_E_STATE`, which is what the other
twenty-eight already answered in the same situation.

The same for an acquisition that `katherine_acquisition_init()` never
filled, or that `katherine_acquisition_fini()` has released:
`katherine_acquisition_begin()`, `_abort()`, `_stop()` and `_read()`
answer `KATHERINE_E_STATE` rather than following a null device. An
acquisition is spent after `fini()`, not reset; reuse means `init()`
again.

`katherine_transport_get_last_os_error()` answers 0 on a transport with no
link. It is where a failure sends the caller, and a failure early enough
leaves nothing open, so it was the one call that could not be made after
the failure it reports on. The other transport operations still require a
link the caller opened.

### Arguments that cannot be carried are refused

`katherine_set_acq_time()` and `katherine_set_bias()` refuse a value that
is not a number, and the shutter time also refuses a negative one and one
too large for the readout's tick count; the conversion of a NaN to the
wire format was undefined behaviour. `katherine_tpx3_set_acq_mode()`
refuses a pixel mode outside the enumeration, which used to reach the
sensor as the low two bits of whatever was passed -- a combination the
chip does not define. DAC values and sensor register indices are still
transmitted unchecked, as 1.x transmitted them, with
`katherine_tpx3_dacs_validate()` the opt-in check.

`katherine_tpx3_get_dac_monitor_voltage()` and `_voltages()` are refused
while an acquisition is running, as `katherine_get_sensor_temperature()`
already was: the readout answers the scan on the data lanes a measurement
is using.

### The measurement word says which ASIC it is

`KATHERINE_MD_SIZE` is `KATHERINE_TPX3_MD_SIZE`. It is six bytes for a
Timepix3 readout and eight for a Timepix4 one, so an unprefixed constant
cannot carry both -- and this is the last window in which it can be
renamed, a public macro being stuck once the tag ships. WP19's rule asks
for exactly this of every symbol that will branch on the ASIC; this one was
missed by the sweep and found while reading the Timepix4 manual against
Track Lab's readout.

The wrappers follow: C++'s `katherine::md_size` is
`katherine::tpx3::md_size` and Python's `MD_SIZE()` is `TPX3_MD_SIZE()`.
`katherine1.h` aliases the 1.x spelling, and `test_compat1.c` checks both
that the alias exists and that it is still six.

### Two error codes, and a reserved word that must be zero

`KATHERINE_E_BAD_RESERVED` and `KATHERINE_E_BUSY` join the error domain.

The first is what makes a `reserved[6]` word mean something. Both
configuration structures carry one, after `struct kvm_cpuid2`, and that
precedent has two halves: the words exist, *and* a non-zero word is
refused. Only the first half had been adopted, which would have left a
later version unable to give the words meaning -- a caller who had put
anything there would have been silently reinterpreted.
`katherine_tpx3_configure()` refuses both reserved blocks when they are
not zero, and the documented idiom is still `calloc` plus the fields you
mean.

The second distinguishes a port another process is holding from an address
this library cannot parse, both of which used to be
`KATHERINE_E_ADDRESS`. What counts as "held" is not the same on both
platforms, and the header says so. **Linux** sets `SO_REUSEADDR` and two
libkatherine sessions therefore share a port; Windows does not set it and
refuses the second session outright; and macOS refuses it too, `SO_REUSEADDR`
not covering an exact unicast duplicate on the BSDs, which want
`SO_REUSEPORT`. Linux is the permissive one, not POSIX -- measured on all
three.

### Closing a device parks its bias supplies

`katherine_device_fini()` drives every bias supply the readout has to a
small positive value before it lets go of the link. A session that ends --
or crashes out of a `with` block, or returns early on an error -- no longer
leaves a sensor biased.

**There is no opt-out flag.** A caller who wants the bias left standing has
to keep the device open.

The value requested is `KATHERINE_BIAS_PARK_VOLTS`, 0.001 V, and not zero,
because the readout's firmware reads the **sign** of the requested float as
a polarity request: `biasSetting()` divides by `ADC_COMP` and tests `> 0`,
so 0 V takes the negative arm. 0.001 V takes the positive arm and lands
below the supply's 0.1953 V step, so the supply reads zero with its
polarity undisturbed.

Measured on a two-chip second-generation readout, two runs from a power
cycle: both supplies fall from 300 V to their own off-state readings in
about three seconds, the leakage falls with them, and no polarity moves.
The second run opens on the state the first run's park left behind and
reaches the rail in three seconds rather than four -- which is the test for
a relay that flipped and stuck.

### A first-generation readout refuses the timing counters

`katherine_get_tdc_counts()` returns `KATHERINE_E_UNSUPPORTED` on a
first-generation readout instead of sending the command. Opcode `0x33`'s
entry in both Gen1 dispatchers is the default arm, so the command used to
go out and the call then waited for one answer per channel, each of which
could only time out. The channels themselves exist there and
`katherine_tpx3_set_tdc_config()` reaches them; it is reading their
counters that the firmware never implemented.

### A capability count of zero means none of that hardware

Four rows of the capability table report no GPIO, no timing channels and no
pulse channels, and two of them are readouts this library drives. Zero now
means "none" rather than "unknown": `katherine_set_gpio_config()` refuses
instead of writing three registers to hardware that cannot have obeyed, and
`katherine_get_tdc_counts()` refuses instead of sending a command whose
answers it then does not read -- which poisons the next command. The pulse
generator gates on its own count rather than on the readout generation,
which is the same answer on every row of today's table and still right if a
later row pairs one with the other.

The table also records **what each readout's bias supplies can emit**, as
`bias_min_volts` and `bias_max_volts`, and `katherine_set_bias()` refuses
what is outside it. A row where nobody has established the range carries
NaN and refuses nothing. **This is a supply capability and not an assembly's
safe range** -- a readout able to produce ±1000 V is routinely wired to a
sensor that must never see more than 300 V, and nothing in this library
knows which assembly is attached.

### The handler table has room for a handler 2.1 adds

`katherine_acquisition_handlers_t` had four function pointers and nothing
after them, so adding a fifth would move `katherine_acquisition_t::priv`
and everything behind it -- an ABI break for a change that ought to be
additive.

It gains `void (*reserved[4])(void)`, which takes the table to a round 64
bytes. Leave the slots NULL: `katherine_acquisition_begin()` answers
`KATHERINE_E_BAD_RESERVED` if any holds a handler, which is the same
contract the configuration's reserved words already carry and for the same
reason -- a caller who put something there would be silently reinterpreted
the moment a slot is given a meaning.

Four rather than three because four is where the table lands on 64 bytes,
for eight bytes more than three. Two is the one not to take: the first
addition after it leaves the same awkward remainder this exists to avoid.

### A frame's timestamps are per chip, because the readout sends them per chip

Measured on the two-chip readout on 2026-10-28, not inferred: every frame
carries **one tail per chip**, four words each, and the first of the four
names its chip in bits [35:32]. The two chips' timers do not agree. They
sat a **constant 2 214 636 ticks apart -- 55.4 ms** -- in all four frames
of the run, for both the start and the end.

`katherine_frame_info_t::start_time` and `::end_time` were single values,
so the second chip's tail overwrote the first's and a caller referencing a
chip-0 hit against the reported frame start was out by that much. They are
now arrays indexed by chip, with `reporting_chips` saying how many entries
are filled.

`KATHERINE_MAX_CHIP_COUNT` is 16, sized by **what the measurement word can
say** -- the chip field is four bits -- rather than by what today's
hardware carries. An index off the wire therefore cannot reach the end of
either array, which is why neither needs a bounds check.

The callback signature is unchanged: only two of the record's fields were
ever per chip, and making the record itself per chip would have duplicated
the six that are not. `received_pixels`, `sent_pixels` and `lost_pixels`
stay per frame, which is what the firmware sends -- one `0xD` and one `0xC`
after the tails, both totals.

**The first generation is untouched.** Its tail keeps going to chip 0,
because no first-generation firmware is on this machine to say whether it
fills that field at all. Only the second generation's header map reads it.

C++ sees the change through its `frame_info` alias. Python keeps
`start_time` and `end_time` as the first reporting chip and gains
`start_times`, `end_times` and `reporting_chips`.

### The frame handlers carry the counter's own type

`frame_started` and `frame_ended` took an `int` frame index, and
`katherine_acquisition_t::requested_frames` and `::completed_frames` were
`int` too -- while the `frame_count` they are filled from has been a
`uint32_t` since the configuration split. The narrowing was written out in
`katherine_acquisition_begin()` as an explicit `(int)` cast, which is the
sort of cast that stops looking like a question.

All four are `uint32_t` now, and the cast is gone. The handler typedefs
follow in both wrappers: C++'s `frame_started_handler` and
`frame_ended_handler` take `std::uint32_t`, as do `requested_frames()` and
`completed_frames()`.

Nothing about the values changes -- a frame index was never negative and
`requested_frames` was never more than a `uint32_t` could hold. What
changes is that a caller writing a handler no longer has to guess which
type the library will hand them.

### An acquisition's two timeouts carry their unit and refuse a negative

`katherine_acquisition_init()`'s last two parameters were `int
report_timeout` and `int fail_timeout`. Both are milliseconds, neither said
so, and a non-positive value disabled each -- so three of the four
quadrants of an `int` meant "off" and nothing in the signature hinted at
it.

They are `uint32_t report_timeout_ms` and `uint32_t fail_timeout_ms`, and
the fields of `katherine_acquisition_t` follow. Zero still disables, which
is what the implementation already tested for; what goes away is the
ability to write a negative and wonder. Python's two properties are
renamed to match.

C++ never saw this: it takes `std::chrono` durations and converts, so its
callers expressed the unit in the type all along.

Through the 1.x shim nothing changes, and that took a line rather than a
cast. 1.x disabled either timeout on **any** non-positive value, so the
shim folds negatives onto zero instead of letting them wrap to something
enormous.

### Starting the readout names the chain it starts

`katherine_tpx3_set_seq_readout_start(device, int arg)` is
`katherine_tpx3_start_readout(device, katherine_tpx3_readout_mode_t)`.

Its doc block called the argument "implementation-defined", which the
firmware contradicts. `cmdID_branching()` routes opcode `0x0005` to
`ReadoutStart()`, whose body is three lines: it reads **bit 0** of the
command's first byte and starts the sequential chain on zero, the
data-driven one on one. That is exactly how `katherine_tpx3_readout_mode_t`
numbers them, and exactly what `katherine_acquisition_begin()` has always
passed -- the `int` was that enumeration everywhere except in the
declaration.

**Renamed as well as retyped**, because the old name was narrower than the
call: it does not start a *sequential* readout, it starts whichever chain
the argument names, and `ReadoutStart()` is what the firmware calls it.

C++ is `start_readout(katherine::tpx3::readout_mode)` and Python is
`start_readout(readout_mode)`. The 1.x spelling still takes an `int`
through the shim and casts, since the values were always this
enumeration's.

### The shutter time is named for what it is, and measured in seconds

`katherine_config_t::acq_time` was the only duration in the public surface
carried in nanoseconds. Every other one -- `delay_seconds`,
`length_seconds`, `low_seconds`, `high_seconds` -- is in seconds, and two
of those are reachable through the same configuration object, one level
down in the embedded triggers. `acq_time = 1.0` and
`start_trigger.delay_seconds = 1.0` were both legal `double`s meaning
durations a billion apart.

It is now `frame_duration_seconds`, which says the unit and pairs with
`frame_count` beside it. `katherine_set_acq_time()` is
`katherine_set_frame_duration()`.

**The rename is what makes the unit change safe.** Changing the unit alone
would have compiled everywhere and silently asked for a shutter a billion
times too long. Renaming alone would have left the odd unit. Doing both at
once stops the compiler at every call site, and 2.0 is the only release
that can.

The wrappers follow: C++ has `frame_duration()` and `set_frame_duration()`
on a `std::chrono::duration`, so its callers never see a unit; Python has
`frame_duration_seconds`. Through the 1.x shim nothing changes at all --
`katherine_set_acq_time()` still takes nanoseconds, and that one call is
the only place in `katherine/katherine1.h` that converts rather than
forwards.

### A pulse channel is numbered as the array that configures it

`KATHERINE_GPIO_PULSE_CHANNEL_0` was the output of
`katherine_pulse_config_t::channel[1]`, because `channel[0]` is the
generator's internal channel and has no output gate of its own. Two
zero-based numberings of one generator, one apart, and the library's own
prose already used the other one -- `pulse.c` says "channels 1 to 3".

The three enumerators become `KATHERINE_GPIO_PULSE_OUT_1`, `_2` and `_3`,
where the digit is the `channel[]` index. The values are the firmware's mux
codes and do not move, so this is a source break and not an ABI one.

**Renamed rather than renumbered, deliberately.** Shifting
`PULSE_CHANNEL_{0,1,2}` to `{1,2,3}` would have left two live names
meaning something one apart from what they meant, and nothing would have
failed to compile. No old spelling survives this, so every stale call site
is a compiler error. `KATHERINE_GPIO_PULSE_INTERNAL` already named
`channel[0]` and is unchanged.

### The emulator sends a frame tail per chip

A frame's start and end timestamps are four measurement-data words, and a
second-generation readout sends the whole set **once per chip**, after the
hits, naming the chip in the first of the four. The emulator sent one set
whatever `chip_count` asked for, so no test and no consumer of
`katherine/emulator.h` could see the stream a two-chip readout produces.

It now follows the firmware for that generation. The first generation's
order is unchanged: one chip, no chip index, and the start timestamps
before the hits. `katherine_emu_stream_t` gains an internal field.

The library's decoder is unchanged: it keeps one `katherine_frame_info_t`
and the last chip's tail wins, which is a decision rather than an
oversight and is recorded where the timestamps are handled.

### Smaller additions a consumer can test for

`KATHERINE_TPX3_CONFIG_SIZE(chip_count)` gives the byte size of a
configuration for that many chips, which a caller allocating one had to
work out from the flexible array by hand.

`KATHERINE_FEATURE_EMULATOR` joins the per-transport feature macros in
`katherine/features.h`, so asking whether the emulator is in a build no
longer means testing for its header.

In C++, `katherine::tpx3::dac_to_si()` returns a `dac_si{value, unit}`
rather than taking a reference the caller must declare and initialise
first, which is the shape `tpx3::timestamp_seconds` and `tpx3::toa_ftoa`
already had.

### An acquisition counts the bytes it took off the link

`katherine_acquisition_t` gains `received_measurement_data`, the number of
bytes read from the data link, counted whether or not the stream is
decoded. `dropped_measurement_data` and `truncated_measurement_data` say
what was lost; this says what arrived, which is what a caller needs to
tell a quiet readout from a quiet link. Mirrored in the C++ and Python
acquisitions.

### The USB transport states its libusb floor

Configuring with `KATHERINE_TRANSPORT_USB` on a libusb older than 1.0.27
now fails at configure time with the reason, instead of configuring
cleanly and failing at the link with an unhelpful error: the code uses
`libusb_init_context()`, which that release introduced.

### A pixel buffer too small for one hit is refused

`katherine_acquisition_begin()` refuses with `KATHERINE_E_INVAL` when the
pixel buffer `katherine_acquisition_init()` was given cannot hold a single
hit of the mode and oscillator asked for. The decode loop used to write
past the end of it on every decoded hit. The check is at `begin()` and not
at `init()`, because the width of a hit depends on the pixel mode and the
fast oscillator, and neither is known until then; a raw stream writes no
hits and still needs no pixel buffer.

### Frame boundaries survive a lost frame-finished datum

`frame_started` and `frame_ended` are now raised in pairs. A frame still
open when the next one starts -- which is what a lost frame-finished datum
leaves behind, and on UDP a lost datagram is all it takes -- is reported
ended and incomplete before the new one is announced. It used to be
dropped silently, so two frames' hits reached the handler as one with
nothing to say so.

Callers see one more `frame_ended` than before, for a frame they were
previously told about only by `frame_started`. The `int` both handlers
carry is `katherine_acquisition_t::completed_frames`, a running count
rather than an identifier: a frame the readout abandons does not raise it,
so the next frame is announced with the same number.

### Smaller

`katherine_strerror(KATHERINE_E_IO)` reads "i/o error" rather than
"transport i/o error", and the enumerator names both the link case and the
file case -- `katherine_tpx3_px_config_load_bmc_file()` returns it for a
calibration file that cannot be opened, which is not a transport failing.

The `krun` examples take the readout's address as their first argument in
all three languages, as only the C one did; the C++ one reports a readout
that does not answer instead of ending in `std::terminate`; and all three
report a missing `chipconfig.bmc` the same way.

A closed Python `Device` or `Acquisition` renders as
`<katherine.Device [closed]>` rather than raising, which is what a closed
file does; every other member still raises `ValueError`. The Wireshark
dissector reads the trigger word at the offsets the library has been
sending since the clock rework, and shows both generations' layouts.

## 1.1.0 — 2026-08-25

The last release of the 1.x line before the 2.0 redesign. Highlights:

- Readout emulator (library core + the `ksim` UDP daemon), test suite and
  benchmarks — a complete hardware-free development story, in CI on
  Linux, macOS and Windows.
- A long line of protocol-level fixes: frame-timestamp word order,
  pixel-configuration recovery, upload pacing, stray-datagram session
  retargeting, command-encoder overruns, `gray_disable`, abort handling,
  ToA-offset leak, partial-word decode, and more.
- Language surfaces completed and harmonized: full Cython bindings built
  natively by CMake, the C++ wrapper's remaining slow-control setters,
  UDP sessions in all three languages, and debug stringification
  everywhere (C, C++, Python, GDB, LLDB — byte-identical).
- Opt-in DAC range validation; documented measurement-buffer contract
  with a truncation counter; Wireshark dissector installed system-wide.
- Build: language standards selectable from C11/C++11 up, pedantic
  warnings clean, in-repo debugger pretty-printers.

## 1.0.0 — 2026-08-19

First tagged release of the 1.x API as historically published.
