# Katherine Wireshark dissector

`katherine.lua` decodes the Katherine Timepix3 readout UDP protocol: 8-byte control commands/responses on port 1555, and the 6-byte measurement-data (MD) word stream on port 1556. Layouts come from `c/src/command_interface.h`, `c/include/katherine/config.h`, `c/src/crd.h` and `c/src/md.h` in this repository.

Load with `wireshark -X lua_script:katherine.lua`, or drop the file into your personal Lua plugins folder (Wireshark: *Help -> About Wireshark -> Folders*) to load it automatically every run. The CMake install step also deposits it into the global Lua plugins folder (`lib/wireshark/plugins` under the install prefix; override with `KATHERINE_WIRESHARK_PLUGIN_DIR`), so a system-wide install of libkatherine makes the dissector load automatically for every user.

The **Pixel data layout** preference (`katherine.mode`) selects which of the six `md.h` pixel-hit layouts decodes header-0x4 words: `f_toa_tot` (default), `toa_tot`, `f_toa_only`, `toa_only`, `f_event_itot`, `event_itot`. It must match the acquisition mode active when the capture was taken; the wire format alone cannot disambiguate it.

Example:

    tshark -r capture.pcap -X lua_script:katherine.lua -o "katherine.mode: event_itot" -Y katherine
