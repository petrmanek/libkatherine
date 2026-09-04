--[[
Wireshark dissector for the Katherine Timepix3 readout UDP protocol.

Katherine exposes two independent UDP services:

  * Control plane (UDP port 1555): fixed-size 8-byte request/response
    datagrams used to configure the readout and query its status.
  * Data plane (UDP port 1556): datagrams consisting of a run of 6-byte
    little-endian "measurement data" (MD) words, streamed while (or after)
    an acquisition is running.

All field layouts below were taken from the reference implementation
shipped in this repository -- they are the source of truth:

  * c/src/command_interface.h  - control opcodes, HW sub-commands, DAC
                                  sub-indices (CMD_TYPE_INTERNAL_DAC_SETTINGS)
  * c/include/katherine/config.h - sensor register sub-indices
                                  (katherine_tpx3_reg_t), acquisition setup
                                  and test pulse byte layouts (see
                                  katherine_acquisition_setup() and
                                  katherine_set_test_pulses() in
                                  c/src/config.c)
  * c/src/crd.h                - control response bitfields (readout
                                  status, communication status)
  * c/src/md.h                 - measurement data word bitfields, including
                                  the six per-acquisition-mode pixel layouts
  * c/src/acquisition.c        - MD header dispatch (which header nibble
                                  means what)
  * c/src/status.c             - control response payload semantics
                                  (floats for temperature/ADC/bias, chip ID
                                  decoding, comm status data rate scaling)

Load with:
  wireshark -X lua_script:katherine.lua
  tshark    -X lua_script:katherine.lua -r capture.pcap
--]]

local katherine_proto = Proto("katherine", "Katherine TPX3 Readout Protocol")

----------------------------------------------------------------------
-- Small helpers
----------------------------------------------------------------------

-- Extract `width` bits starting at bit `shift` from a plain Lua number.
-- Used only for composite values that Wireshark's field mask/shift display
-- cannot express directly (e.g. the chip ID decoding below, which requires
-- subtracting 1 from an extracted nibble).
local function bits(value, shift, width)
    return math.floor(value / (2 ^ shift)) % (2 ^ width)
end

-- Read a little-endian 48-bit value (an MD word) out of a 6-byte TvbRange
-- as a plain Lua number. 2^48 is far below 2^53, so this is exact.
local function le48(range)
    local lo = range(0, 4):le_uint()
    local hi = range(4, 2):le_uint()
    return lo + hi * 4294967296.0
end

----------------------------------------------------------------------
-- Control plane: opcode / sub-index / value_string lookup tables
----------------------------------------------------------------------

-- katherine_cmd_type_t, c/src/command_interface.h
local OPCODE_NAMES = {
    [0x01] = "Acquisition time settings (LSB)",
    [0x02] = "Bias settings",
    [0x03] = "Acquisition start",
    [0x04] = "Internal DAC settings",
    [0x05] = "Sequential readout start",
    [0x06] = "Acquisition stop",
    [0x07] = "HW command start",
    [0x08] = "Sensor register setting",
    [0x09] = "Acquisition mode setting",
    [0x0A] = "Acquisition time setting (MSB)",
    [0x0B] = "Echo chip ID",
    [0x0C] = "Get bias voltage",
    [0x0D] = "Get ADC voltage",
    [0x0E] = "Get back-read register",
    [0x0F] = "Internal DAC scan",
    [0x10] = "Set pixel config",
    [0x11] = "Get pixel config",
    [0x12] = "Set all pixel config",
    [0x13] = "Number of frames",
    [0x14] = "Get all DAC scan",
    [0x15] = "Get HW readout temperature",
    [0x16] = "LED settings",
    [0x17] = "Get readout status",
    [0x18] = "Get communication status",
    [0x19] = "Get sensor temperature",
    [0x20] = "Digital test",
    [0x21] = "Acquisition setup",
    [0x22] = "Get acquisition unit data",
    [0x23] = "Internal trigger generator",
    [0x26] = "Test pulse setting",
    [0x28] = "ToA calibration setup",
    [0x29] = "Number of tokens setting",
    [0x30] = "Get bias current",
    [0x32] = "Internal TDC settings",
    [0x33] = "Internal TDC read counts",
    [0x50] = "Interface selection",
    [0xF0] = "Change ports",
}

-- katherine_hw_cmd_type_t, c/src/command_interface.h (opcode 0x07, byte[0])
local HW_SUBCMD_NAMES = {
    [0]  = "Sensor config registers update",
    [1]  = "Internal DAC update",
    [2]  = "Internal DAC back-read",
    [3]  = "Timer read",
    [4]  = "Timer set",
    [5]  = "Reset matrix sequential",
    [6]  = "Stop matrix command",
    [7]  = "Load column test-pulse register",
    [8]  = "Read column test-pulse register",
    [9]  = "Load pixel register configuration",
    [10] = "Read pixel register configuration",
    [11] = "Read pixel matrix sequential (setting)",
    [12] = "Read pixel matrix data-driven (setting)",
    [13] = "Chip ID read",
    [14] = "Output block config update",
    [15] = "Digital test",
}

-- katherine_dacs_named_t / CMD_TYPE_INTERNAL_DAC_SETTINGS sub-index,
-- c/include/katherine/config.h + c/src/command_interface.h
local DAC_NAMES = {
    [0]  = "Ibias_Preamp_ON",
    [1]  = "Ibias_Preamp_OFF",
    [2]  = "VPReamp_NCAS",
    [3]  = "Ibias_Ikrum",
    [4]  = "Vfbk",
    [5]  = "Vthreshold_fine",
    [6]  = "Vthreshold_coarse",
    [7]  = "Ibias_DiscS1_ON",
    [8]  = "Ibias_DiscS1_OFF",
    [9]  = "Ibias_DiscS2_ON",
    [10] = "Ibias_DiscS2_OFF",
    [11] = "Ibias_PixelDAC",
    [12] = "Ibias_TPbufferIn",
    [13] = "Ibias_TPbufferOut",
    [14] = "VTP_coarse",
    [15] = "VTP_fine",
    [16] = "Ibias_CP_PLL",
    [17] = "PLL_Vcntrl",
}

-- katherine_tpx3_reg_t, c/include/katherine/config.h (opcode 0x08, byte[4])
local SENSOR_REG_NAMES = {
    [0]  = "TEST_PULSE_METHOD",
    [1]  = "NUMBER_TEST_PULSES",
    [2]  = "OUT_BLOCK_CONFIG",
    [3]  = "PLL_CONFIG",
    [4]  = "GENERAL_CONFIG",
    [5]  = "SLVS_CONFIG",
    [6]  = "POWER_PULSING_PATTERN",
    [7]  = "SET_TIMER_LOW",
    [8]  = "SET_TIMER_MID",
    [9]  = "SET_TIMER_HIGH",
    [10] = "SENSE_DAC_SELECTOR",
    [11] = "EXT_DAC_SELECTOR",
}

-- katherine_tpx3_px_mode_t, c/include/katherine/config.h (opcode 0x09, byte[0] bits 0-6)
local ACQ_MODE_NAMES = {
    [0] = "KATHERINE_TPX3_PX_TOA_TOT",
    [1] = "KATHERINE_TPX3_PX_ONLY_TOA",
    [2] = "KATHERINE_TPX3_PX_EVENT_COUNT_ITOT",
}

local YES_NO = { [0] = "No", [1] = "Yes" }

----------------------------------------------------------------------
-- Data plane: MD header nibble names, c/src/acquisition.c dispatch
----------------------------------------------------------------------

local MD_HEADER_NAMES = {
    [0x2] = "Trigger info",
    [0x3] = "Trigger info",
    [0x4] = "Pixel hit",
    [0x5] = "Timestamp offset (data-driven mode)",
    [0x7] = "New frame",
    [0x8] = "Frame start time (LSB)",
    [0x9] = "Frame start time (MSB)",
    [0xA] = "Frame end time (LSB)",
    [0xB] = "Frame end time (MSB)",
    [0xC] = "Frame finished",
    [0xD] = "Lost pixel count",
    [0xE] = "Acquisition aborted",
}

----------------------------------------------------------------------
-- Preference: pixel MD layout (mirrors the DEFINE_PMD_MAP variants in
-- c/src/md.h). This cannot be determined from the wire format itself --
-- it must match the acquisition mode / fast-VCO setting that was active
-- when the capture was made.
----------------------------------------------------------------------

local PIXEL_LAYOUTS = {
    { "f_toa_tot",     "f_toa_tot: ftoa[0:3] tot[4:13] toa[14:27] (fast VCO, default)", 1 },
    { "toa_tot",       "toa_tot: hit_count[0:3] tot[4:13] toa[14:27]",                  2 },
    { "f_toa_only",    "f_toa_only: ftoa[0:3] toa[14:27] (fast VCO)",                   3 },
    { "toa_only",      "toa_only: hit_count[0:3] toa[14:27]",                           4 },
    { "f_event_itot",  "f_event_itot: hit_count[0:3] event_count[4:13] integral_tot[14:27] (fast VCO)", 5 },
    { "event_itot",    "event_itot: event_count[4:13] integral_tot[14:27]",             6 },
}

katherine_proto.prefs.mode = Pref.enum(
    "Pixel data layout",
    1,
    "Acquisition-mode pixel field layout used to decode header-0x4 (pixel hit) MD words; " ..
    "mirrors the six DEFINE_PMD_MAP layouts in c/src/md.h. Must match the acquisition/fast-VCO " ..
    "mode the capture was taken under -- this cannot be inferred from the wire format.",
    PIXEL_LAYOUTS,
    false)

----------------------------------------------------------------------
-- Protocol fields
----------------------------------------------------------------------

-- Control plane
local f_direction        = ProtoField.string("katherine.direction", "Direction")
local f_opcode           = ProtoField.uint8("katherine.opcode", "Opcode", base.HEX, OPCODE_NAMES)
local f_dac_index        = ProtoField.uint8("katherine.dac_index", "DAC index", base.DEC, DAC_NAMES)
local f_sensor_register  = ProtoField.uint8("katherine.sensor_register", "Sensor register", base.DEC, SENSOR_REG_NAMES)
local f_sub_address       = ProtoField.uint8("katherine.sub_address", "Sub-address", base.HEX)
local f_hw_subcommand    = ProtoField.uint8("katherine.hw_subcommand", "HW sub-command", base.DEC, HW_SUBCMD_NAMES)

local f_payload_bytes    = ProtoField.bytes("katherine.payload", "Payload (bytes 0-3, raw)")
local f_payload_float    = ProtoField.float("katherine.payload_float", "Payload (float)")
local f_payload_uint     = ProtoField.uint32("katherine.payload_uint", "Payload (unsigned integer)")

-- opcode 0x09: acquisition mode setting (byte[0] packed flags)
local f_acq_mode         = ProtoField.uint8("katherine.px_mode", "Acquisition mode", base.DEC, ACQ_MODE_NAMES, 0x7F)
local f_fast_vco         = ProtoField.uint8("katherine.fast_vco_enabled", "Fast VCO enabled", base.DEC, YES_NO, 0x80)

-- opcode 0x21: acquisition setup (byte[0]/byte[1] packed trigger flags)
local f_start_trig_en     = ProtoField.uint8("katherine.start_trigger.enabled", "Start trigger enabled", base.DEC, YES_NO, 0x01)
local f_start_trig_ch     = ProtoField.uint8("katherine.start_trigger.channel", "Start trigger channel", base.DEC, nil, 0x0E)
local f_start_trig_fall   = ProtoField.uint8("katherine.start_trigger.falling_edge", "Start trigger falling edge", base.DEC, YES_NO, 0x10)
local f_delayed_start     = ProtoField.uint8("katherine.delayed_start", "Delayed start", base.DEC, YES_NO, 0x20)
local f_end_trig_en       = ProtoField.uint8("katherine.end_trigger.enabled", "End trigger enabled", base.DEC, YES_NO, 0x01)
local f_end_trig_ch        = ProtoField.uint8("katherine.end_trigger.channel", "End trigger channel", base.DEC, nil, 0x0E)
local f_end_trig_fall       = ProtoField.uint8("katherine.end_trigger.falling_edge", "End trigger falling edge", base.DEC, YES_NO, 0x10)

-- opcode 0x26: test pulse setting
local f_tp_count          = ProtoField.uint16("katherine.test_pulse.count", "Test pulse count", base.DEC)
local f_tp_period_raw     = ProtoField.uint8("katherine.test_pulse.period_raw", "Test pulse period (raw register byte)", base.DEC)
local f_tp_phase          = ProtoField.uint8("katherine.test_pulse.phase", "Test pulse phase", base.DEC)
local f_tp_digital_only   = ProtoField.uint8("katherine.test_pulse.digital_only", "Digital only", base.DEC, YES_NO, 0x01)
local f_tp_external       = ProtoField.uint8("katherine.test_pulse.external", "External source", base.DEC, YES_NO, 0x02)
local f_tp_enabled        = ProtoField.uint8("katherine.test_pulse.enabled", "Enabled", base.DEC, YES_NO, 0x04)

-- opcode 0x17 response: readout status (c/src/crd.h: readout_status_crd)
local f_hw_type           = ProtoField.uint8("katherine.readout_status.hw_type", "HW type", base.DEC)
local f_hw_revision       = ProtoField.uint8("katherine.readout_status.hw_revision", "HW revision", base.DEC)
local f_hw_serial         = ProtoField.uint16("katherine.readout_status.serial", "Serial number", base.DEC)
local f_fw_version        = ProtoField.uint16("katherine.readout_status.fw_version", "Firmware version", base.DEC)

-- opcode 0x18 response: communication status (c/src/crd.h: comm_status_crd)
local f_comm_lines_mask     = ProtoField.uint8("katherine.comm_status.lines_mask", "Comm lines mask", base.HEX)
local f_comm_rate_raw       = ProtoField.uint8("katherine.comm_status.data_rate_raw", "Data rate (raw register)", base.DEC)
local f_comm_rate_mbps      = ProtoField.uint32("katherine.comm_status.data_rate_mbps", "Data rate (Mbps)", base.DEC)
local f_comm_chip_detected  = ProtoField.uint8("katherine.comm_status.chip_detected", "Chip detected flag", base.DEC)

-- opcode 0x0B response: echo chip ID (c/src/status.c: katherine_get_chip_id)
local f_chip_id            = ProtoField.string("katherine.chip_id", "Decoded chip ID")

-- opcode 0x20 response: digital test result (c/src/status.c: crd[0] == 64 means OK)
local f_digital_test_ok    = ProtoField.uint8("katherine.digital_test.result_byte", "Digital test result byte (64 = OK)", base.DEC)

-- Bulk pixel-matrix upload on the control port, triggered by opcode 0x12
-- (katherine_set_all_pixel_config() in c/src/config.c streams the 64 KiB
-- configuration matrix as 64 raw 1024-byte writes, outside the normal
-- 8-byte command framing). The same function's error-recovery path
-- (recover_from_incomplete_set_all_pixel_config()) floods 3*64 further
-- 1024-byte datagrams, each filled with the single repeated byte
-- CMD_TYPE_GET_HW_READOUT_TEMPERATURE (0x15), to resynchronize the command
-- stream after a lost/short write.
local f_pixel_chunk_data   = ProtoField.bytes("katherine.pixel_config_chunk", "Pixel configuration data")

-- Data plane (MD words)
local f_md_header          = ProtoField.uint8("katherine.md.header", "MD header", base.HEX, MD_HEADER_NAMES, 0xF0)
local f_md_coord_x         = ProtoField.uint16("katherine.md.coord_x", "Pixel X", base.DEC, nil, 0x0FF0)
local f_md_coord_y         = ProtoField.uint16("katherine.md.coord_y", "Pixel Y", base.DEC, nil, 0x0FF0)

local f_md_ftoa            = ProtoField.uint32("katherine.md.ftoa", "fToA", base.DEC, nil, 0x0000000F)
local f_md_hit_count       = ProtoField.uint32("katherine.md.hit_count", "Hit count", base.DEC, nil, 0x0000000F)
local f_md_tot             = ProtoField.uint32("katherine.md.tot", "ToT", base.DEC, nil, 0x00003FF0)
local f_md_event_count     = ProtoField.uint32("katherine.md.event_count", "Event count", base.DEC, nil, 0x00003FF0)
local f_md_toa             = ProtoField.uint32("katherine.md.toa", "ToA (raw, pre-offset)", base.DEC, nil, 0x0FFFC000)
local f_md_integral_tot    = ProtoField.uint32("katherine.md.integral_tot", "Integral ToT", base.DEC, nil, 0x0FFFC000)

local f_md_n_sent          = ProtoField.uint64("katherine.md.n_sent", "Pixels sent this frame", base.DEC, nil, 0xFFFFFFFFFFF)
local f_md_n_lost          = ProtoField.uint64("katherine.md.n_lost", "Lost pixel count", base.DEC, nil, 0xFFFFFFFFFFF)
local f_md_trigger_raw     = ProtoField.uint64("katherine.md.trigger_payload", "Trigger info payload (raw; undefined layout)", base.HEX, nil, 0xFFFFFFFFFFF)

local f_md_time_lsb        = ProtoField.uint32("katherine.md.time_lsb", "Timestamp LSB", base.DEC)
local f_md_time_msb        = ProtoField.uint16("katherine.md.time_msb", "Timestamp MSB", base.DEC)
local f_md_time_offset     = ProtoField.uint32("katherine.md.time_offset", "ToA offset (data-driven mode)", base.DEC)
local f_md_new_frame_extra = ProtoField.uint16("katherine.md.new_frame_offset", "New-frame offset field (declared in md.h, unused by reference implementation)", base.DEC, nil, 0x0FFF)

local f_md_truncated       = ProtoField.bytes("katherine.md.truncated", "Truncated trailing word")

katherine_proto.fields = {
    f_direction, f_opcode, f_dac_index, f_sensor_register, f_sub_address, f_hw_subcommand,
    f_payload_bytes, f_payload_float, f_payload_uint,
    f_acq_mode, f_fast_vco,
    f_start_trig_en, f_start_trig_ch, f_start_trig_fall, f_delayed_start,
    f_end_trig_en, f_end_trig_ch, f_end_trig_fall,
    f_tp_count, f_tp_period_raw, f_tp_phase, f_tp_digital_only, f_tp_external, f_tp_enabled,
    f_hw_type, f_hw_revision, f_hw_serial, f_fw_version,
    f_comm_lines_mask, f_comm_rate_raw, f_comm_rate_mbps, f_comm_chip_detected,
    f_chip_id, f_digital_test_ok, f_pixel_chunk_data,
    f_md_header, f_md_coord_x, f_md_coord_y,
    f_md_ftoa, f_md_hit_count, f_md_tot, f_md_event_count, f_md_toa, f_md_integral_tot,
    f_md_n_sent, f_md_n_lost, f_md_trigger_raw,
    f_md_time_lsb, f_md_time_msb, f_md_time_offset, f_md_new_frame_extra,
    f_md_truncated,
}

----------------------------------------------------------------------
-- Control plane dissector (UDP port 1555, 8-byte requests/responses)
----------------------------------------------------------------------

local CONTROL_PORT = 1555
local DATA_PORT    = 1556

-- Length and fill byte of the two non-command datagram shapes that
-- legitimately appear on the control port; see f_pixel_chunk_data above.
local PIXEL_CHUNK_LEN = 1024
local RECOVERY_FILLER_BYTE = 0x15
local RECOVERY_FILLER_STRING = string.rep(string.char(RECOVERY_FILLER_BYTE), PIXEL_CHUNK_LEN)

local function dissect_control(buffer, pinfo, tree)
    pinfo.cols.protocol = "KATHERINE-CTRL"

    local len = buffer:len()

    if len == PIXEL_CHUNK_LEN then
        -- Raw pixel-matrix upload data (or its error-recovery filler), not
        -- an 8-byte command. Distinguish the two by content, not just size.
        local is_filler = (buffer:raw(0, len) == RECOVERY_FILLER_STRING)
        local label = is_filler and "Recovery filler chunk" or "Pixel configuration data chunk"

        local root = tree:add(katherine_proto, buffer, "Katherine control: " .. label)
        root:add(f_pixel_chunk_data, buffer(0, len))

        pinfo.cols.info:set(is_filler
            and "Recovery filler chunk (1024 bytes)"
            or "Pixel config chunk (1024 bytes)")
        return
    end

    local root = tree:add(katherine_proto, buffer, "Katherine control command")

    if len ~= 8 then
        root:add_expert_info(PI_MALFORMED, PI_ERROR,
            string.format("Expected an 8-byte control datagram or a %d-byte pixel-config chunk, got %d bytes", PIXEL_CHUNK_LEN, len))
        pinfo.cols.info:set(string.format("Katherine control [malformed, %d bytes]", len))
        return
    end

    -- Heuristic: the readout listens for commands on port 1555 and answers
    -- from that same port. A packet addressed *to* 1555 is therefore taken
    -- to be a request, one originating *from* 1555 a response. This is a
    -- heuristic, not a guarantee: a client that itself binds its local
    -- socket to port 1555 (as the C client in this repository does) would
    -- make both directions look like "to port 1555".
    local is_request = (pinfo.dst_port == CONTROL_PORT)
    local direction = is_request and "Request" or "Response"
    root:add(f_direction, direction)

    local opcode = buffer(6, 1):uint()
    root:add(f_opcode, buffer(6, 1))
    local opname = OPCODE_NAMES[opcode] or "Unknown opcode"

    local function generic_payload()
        root:add(f_payload_bytes, buffer(0, 4))
        root:add_le(f_payload_uint, buffer(0, 4))
    end

    if opcode == 0x02 then
        -- Bias settings: byte[0..3] is always an IEEE-754 float, in both
        -- the "set" request and (presumably) its acknowledgement.
        root:add_le(f_payload_float, buffer(0, 4))

    elseif opcode == 0x04 then
        -- Internal DAC settings: byte[4] selects the DAC, byte[0..3] carries
        -- its new value (c/src/command_interface.h: CMD_TYPE_INTERNAL_DAC_SETTINGS).
        root:add(f_dac_index, buffer(4, 1))
        root:add_le(f_payload_uint, buffer(0, 4)):append_text(" (DAC value)")

    elseif opcode == 0x07 then
        -- HW command start: byte[0] is the HW sub-command index.
        root:add(f_hw_subcommand, buffer(0, 1))

    elseif opcode == 0x08 then
        -- Sensor register setting: byte[4] selects the register, byte[0..3]
        -- carries its new value (c/include/katherine/config.h: katherine_tpx3_reg_t).
        root:add(f_sensor_register, buffer(4, 1))
        root:add_le(f_payload_uint, buffer(0, 4)):append_text(" (register value)")

    elseif opcode == 0x09 then
        -- Acquisition mode setting: byte[0] packs the mode (bits 0-6) and
        -- the fast-VCO flag (bit 7). See katherine_set_acq_mode() in
        -- c/src/config.c.
        root:add(f_acq_mode, buffer(0, 1))
        root:add(f_fast_vco, buffer(0, 1))

    elseif opcode == 0x0B then
        -- Echo chip ID: response decodes to "<letter><row>-W000<wafer>"
        -- (c/src/status.c: katherine_get_chip_id()).
        generic_payload()
        if not is_request then
            local raw = buffer(0, 4):le_uint()
            local x = bits(raw, 0, 4) - 1
            local y = bits(raw, 4, 4)
            local w = bits(raw, 8, 12)
            local letter = (x >= 0 and x <= 25) and string.char(65 + x) or "?"
            root:add(f_chip_id, buffer(0, 4), string.format("%s%d-W000%d", letter, y, w))
        end

    elseif opcode == 0x0C or opcode == 0x15 or opcode == 0x19 then
        -- Get bias voltage / readout temperature / sensor temperature:
        -- request carries no payload, response is a float (Volts or degC).
        if is_request then
            generic_payload()
        else
            root:add_le(f_payload_float, buffer(0, 4))
        end

    elseif opcode == 0x0D then
        -- Get ADC voltage: request carries the channel ID in byte[0],
        -- response is a float (Volts).
        if is_request then
            root:add_le(f_payload_uint, buffer(0, 4)):append_text(" (ADC channel ID)")
        else
            root:add_le(f_payload_float, buffer(0, 4))
        end

    elseif opcode == 0x17 then
        -- Get readout status response (c/src/crd.h: readout_status_crd).
        if is_request then
            generic_payload()
        else
            root:add(f_hw_type, buffer(0, 1))
            root:add(f_hw_revision, buffer(1, 1))
            root:add_le(f_hw_serial, buffer(2, 2))
            root:add_le(f_fw_version, buffer(4, 2))
        end

    elseif opcode == 0x18 then
        -- Get communication status response (c/src/crd.h: comm_status_crd).
        -- Mbps scaling of the raw register per katherine_get_comm_status().
        if is_request then
            generic_payload()
        else
            root:add(f_comm_lines_mask, buffer(0, 1))
            local raw_rate = buffer(1, 1):uint()
            root:add(f_comm_rate_raw, buffer(1, 1))
            root:add(f_comm_rate_mbps, buffer(1, 1), raw_rate * 5)
            root:add(f_comm_chip_detected, buffer(2, 1))
        end

    elseif opcode == 0x20 then
        -- Digital test: response byte[0] == 64 signals success
        -- (c/src/status.c: katherine_perform_digital_test()).
        if is_request then
            generic_payload()
        else
            root:add(f_digital_test_ok, buffer(0, 1))
        end

    elseif opcode == 0x21 then
        -- Acquisition setup: byte[4] is a fixed sub-address (0x05 in the
        -- reference implementation), byte[0]/byte[1] pack the start/end
        -- trigger configuration (katherine_acquisition_setup(), c/src/config.c).
        root:add(f_sub_address, buffer(4, 1))
        root:add(f_start_trig_en, buffer(0, 1))
        root:add(f_start_trig_ch, buffer(0, 1))
        root:add(f_start_trig_fall, buffer(0, 1))
        root:add(f_delayed_start, buffer(0, 1))
        root:add(f_end_trig_en, buffer(1, 1))
        root:add(f_end_trig_ch, buffer(1, 1))
        root:add(f_end_trig_fall, buffer(1, 1))

    elseif opcode == 0x26 then
        -- Test pulse setting (katherine_set_test_pulses(), c/src/config.c):
        -- byte[0..1] count, byte[2] period (raw = (period-1)/64),
        -- byte[3] phase, byte[4] flags.
        root:add_le(f_tp_count, buffer(0, 2))
        local period_raw = buffer(2, 1):uint()
        root:add(f_tp_period_raw, buffer(2, 1)):append_text(
            string.format(" (period = %d pixel clocks when not externally sourced)", period_raw * 64 + 1))
        root:add(f_tp_phase, buffer(3, 1))
        root:add(f_tp_digital_only, buffer(4, 1))
        root:add(f_tp_external, buffer(4, 1))
        root:add(f_tp_enabled, buffer(4, 1))

    else
        -- No opcode-specific layout beyond byte[6]; show the generic
        -- little-endian payload. Several of these are plain decimal counts
        -- (e.g. 0x01/0x0A acquisition time, 0x13 number of frames).
        generic_payload()
    end

    pinfo.cols.info:set(string.format("Ctrl %s: %s (0x%02X)", direction, opname, opcode))
end

----------------------------------------------------------------------
-- Data plane dissector (UDP port 1556, runs of 6-byte MD words)
----------------------------------------------------------------------

-- Add the pixel-hit-specific fields to an MD subtree, selecting the field
-- set for the acquisition-mode layout chosen via the plugin preference.
-- All of these fields live inside the first 4 bytes of the MD word
-- (global bits 0-27), so a single 4-byte little-endian read is masked
-- differently per mode.
local function add_pixel_fields(item, word)
    local mode = katherine_proto.prefs.mode

    if mode == 1 then -- f_toa_tot
        item:add_le(f_md_ftoa, word(0, 4))
        item:add_le(f_md_tot, word(0, 4))
        item:add_le(f_md_toa, word(0, 4))
    elseif mode == 2 then -- toa_tot
        item:add_le(f_md_hit_count, word(0, 4))
        item:add_le(f_md_tot, word(0, 4))
        item:add_le(f_md_toa, word(0, 4))
    elseif mode == 3 then -- f_toa_only
        item:add_le(f_md_ftoa, word(0, 4))
        item:add_le(f_md_toa, word(0, 4))
    elseif mode == 4 then -- toa_only
        item:add_le(f_md_hit_count, word(0, 4))
        item:add_le(f_md_toa, word(0, 4))
    elseif mode == 5 then -- f_event_itot
        item:add_le(f_md_hit_count, word(0, 4))
        item:add_le(f_md_event_count, word(0, 4))
        item:add_le(f_md_integral_tot, word(0, 4))
    else -- event_itot (mode == 6)
        item:add_le(f_md_event_count, word(0, 4))
        item:add_le(f_md_integral_tot, word(0, 4))
    end

    -- coord_x (bits 28-35) straddles byte[3]/byte[4]; coord_y (bits 36-43)
    -- straddles byte[4]/byte[5]. Both are read as 2-byte little-endian
    -- windows with a mask, per c/src/md.h.
    item:add_le(f_md_coord_x, word(3, 2))
    item:add_le(f_md_coord_y, word(4, 2))
end

local function dissect_data(buffer, pinfo, tree)
    pinfo.cols.protocol = "KATHERINE-MD"

    local len = buffer:len()
    local n_words = math.floor(len / 6)
    local trailing = len - n_words * 6

    local root = tree:add(katherine_proto, buffer, string.format("Katherine measurement data (%d word%s)", n_words, n_words == 1 and "" or "s"))

    local pixel_count = 0
    local seen = {} -- header nibble -> count
    local last_n_sent, last_n_lost

    for i = 0, n_words - 1 do
        local offset = i * 6
        local word = buffer(offset, 6)
        local val = le48(word)
        local header = bits(val, 44, 4)
        seen[header] = (seen[header] or 0) + 1

        local hname = MD_HEADER_NAMES[header] or "Unknown/reserved"
        local item = root:add(katherine_proto, word, string.format("MD[%d]: %s (0x%X)", i, hname, header))
        item:add(f_md_header, word(5, 1))

        if header == 0x4 then
            pixel_count = pixel_count + 1
            add_pixel_fields(item, word)
        elseif header == 0x2 or header == 0x3 then
            item:add_le(f_md_trigger_raw, word)
        elseif header == 0x5 then
            item:add_le(f_md_time_offset, word(0, 4))
        elseif header == 0x7 then
            item:add_le(f_md_new_frame_extra, word(4, 2))
        elseif header == 0x8 or header == 0xA then
            item:add_le(f_md_time_lsb, word(0, 4))
        elseif header == 0x9 or header == 0xB then
            item:add_le(f_md_time_msb, word(0, 2))
        elseif header == 0xC then
            item:add_le(f_md_n_sent, word)
            last_n_sent = bits(val, 0, 44)
        elseif header == 0xD then
            item:add_le(f_md_n_lost, word)
            last_n_lost = bits(val, 0, 44)
        elseif header == 0xE then
            -- No further fields; the MD itself is the signal.
        end
    end

    -- Build the info-column summary, e.g. "1360 MDs: 1352 pixels, new frame, frame finished".
    local parts = {}
    if seen[0x7] then table.insert(parts, "new frame") end
    if seen[0x8] or seen[0x9] then table.insert(parts, "frame start") end
    if seen[0xA] or seen[0xB] then table.insert(parts, "frame end") end
    if seen[0xC] then
        table.insert(parts, last_n_sent and string.format("frame finished (n_sent=%d)", last_n_sent) or "frame finished")
    end
    if seen[0xD] then
        table.insert(parts, last_n_lost and string.format("lost=%d", last_n_lost) or "lost pixels")
    end
    if seen[0xE] then table.insert(parts, "aborted") end
    if seen[0x2] or seen[0x3] then table.insert(parts, "trigger info") end
    if seen[0x5] then table.insert(parts, "timestamp offset") end
    local unknown = 0
    for h, c in pairs(seen) do
        if MD_HEADER_NAMES[h] == nil and h ~= 0x4 then
            unknown = unknown + c
        end
    end
    if unknown > 0 then table.insert(parts, string.format("unknown=%d", unknown)) end

    local summary = string.format("%d MDs: %d pixel%s", n_words, pixel_count, pixel_count == 1 and "" or "s")
    if #parts > 0 then
        summary = summary .. ", " .. table.concat(parts, ", ")
    end

    if trailing > 0 then
        local trailer = buffer(n_words * 6, trailing)
        root:add(f_md_truncated, trailer):append_text(" [truncated word]")
        summary = summary .. string.format(" + %d trailing byte%s [truncated word]", trailing, trailing == 1 and "" or "s")
    end

    pinfo.cols.info:set(summary)
end

----------------------------------------------------------------------
-- Top-level dissector: dispatch on port
----------------------------------------------------------------------

function katherine_proto.dissector(buffer, pinfo, tree)
    if buffer:len() == 0 then return end

    if pinfo.src_port == DATA_PORT or pinfo.dst_port == DATA_PORT then
        dissect_data(buffer, pinfo, tree)
    elseif pinfo.src_port == CONTROL_PORT or pinfo.dst_port == CONTROL_PORT then
        dissect_control(buffer, pinfo, tree)
    end
end

local udp_port_table = DissectorTable.get("udp.port")
udp_port_table:add(CONTROL_PORT, katherine_proto)
udp_port_table:add(DATA_PORT, katherine_proto)
