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
