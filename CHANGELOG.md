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
report. `katherine_px_f_event_itot_t` exposed a `hit_count` that read
the dummy field, and `katherine_px_event_itot_t` omitted one and
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
