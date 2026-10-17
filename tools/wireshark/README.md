# Katherine Wireshark dissector

`katherine.lua` decodes the Katherine Timepix3 readout UDP protocol: 8-byte control commands/responses on port 1555, and the 6-byte measurement-data (MD) word stream on port 1556. Layouts come from `c/src/protocol/cmd_interface.h`, `c/include/katherine/config.h`, `c/src/protocol/crd.h` and `c/src/protocol/md.h` in this repository.

Load with `wireshark -X lua_script:katherine.lua`, or drop the file into your personal Lua plugins folder (Wireshark: *Help -> About Wireshark -> Folders*) to load it automatically every run. The CMake install step also deposits it into the global Lua plugins folder (`lib/wireshark/plugins` under the install prefix; override with `KATHERINE_WIRESHARK_PLUGIN_DIR`), so a system-wide install of libkatherine makes the dissector load automatically for every user.

Two preferences, neither of which the wire format can settle:

The **Readout generation** preference (`katherine.generation`) selects the measurement-data header map, mirroring `ACQ_HDR_IS_PIXEL_GEN1` and `ACQ_HDR_IS_PIXEL_GEN2` in `c/src/acq/hdr.h`. On `gen1` (default) a pixel arrives under header `0x4` and the trigger info under `0x2` and `0x3`; on `gen2` a pixel arrives under `0x0` to `0x3`, one header per chip, and the trigger event's two halves under `0x4` and `0x6`. The two maps disagree about `0x4`, so reading a second-generation capture with the first generation's map shows every trigger as a pixel with nonsense coordinates and every pixel as unknown -- which is the same defect the library itself carried until the Gen2 decode work.

The **Pixel data layout** preference (`katherine.mode`) selects which of the six `md.h` pixel-hit layouts decodes a pixel word: `f_toa_tot` (default), `toa_tot`, `f_toa_only`, `toa_only`, `f_event_count_itot`, `event_count_itot`. It must match the acquisition mode active when the capture was taken.

`selftest.py` checks both maps against synthetic captures, including the misread above. It needs `tshark` and registers itself as the `test_wireshark_dissector` test where one is found.

Example:

    tshark -r capture.pcap -X lua_script:katherine.lua \
        -o "katherine.generation: gen2" -o "katherine.mode: event_count_itot" -Y katherine
