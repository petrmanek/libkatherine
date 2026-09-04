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

One field is renamed without an alias, deliberately. `katherine_config_t`'s
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

Command responses are now correlated with the request that provoked them,
instead of the next datagram to arrive being taken as the answer: a
session flushes what an earlier exchange left behind before it sends,
steps over responses belonging to no request of its own — counting them
in `katherine_udp_t::stray_command_responses`, and giving up with
`KATHERINE_E_STRAY` rather than waiting forever — and reports a response
of the wrong length as `KATHERINE_E_BAD_CRD`. The identifiers the readout
firmware substitutes for a request's own operation code are accepted by
default; `katherine_udp_set_strict_ack()` turns that tolerance off once a
peer has been shown not to need it.

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
report. `katherine_px_f_event_count_itot_t` exposed a `hit_count` that read
the dummy field, and `katherine_px_event_count_itot_t` omitted one and
discarded a real counter. The field swaps between them, so both structs
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
