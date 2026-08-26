/**
 * @file
 * @brief Command responder of the protocol emulator.
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <katherine/emulator.h>
#include <katherine/error.h>
#include "protocol/command_interface.h"
#include "protocol/crd.h"
#include "emu.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/* The one command still not listed in command_interface.h: opcode 0x27's
 * meaning differs between sources (this firmware lineage calls it ToA
 * calibration start; other implementations assign 0x27 to later chip
 * generations), so it keeps the emulator-local name until adjudicated. */
#define EMU_CMD_TYPE_TOA_CALIBRATION_START 0x27

/* Communication status the emulator reports: all eight data lines up, one
 * sensor chip attached. */
#define KATHERINE_EMU_COMM_LINES_MASK      0xFF
#define KATHERINE_EMU_CHIP_COUNT           1

/* Number of matrix patterns the digital test walks through; all of them
 * pass, which is what the library requires to accept the result. */
#define KATHERINE_EMU_DIGITAL_TEST_PASSES  64

static inline float
load_float(const uint8_t *src)
{
    /* The readout is little-endian and the library reinterprets the
       response bytes as a host float, so the payload is an IEEE-754
       single in little-endian byte order. */
    uint32_t bits = katherine_emu_load_le32(src);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline void
store_float(uint8_t *dst, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    katherine_emu_store_le(dst, bits, 4);
}

/* The library prints the chip identifier as "%c%d-W000%d" from the three
 * fields packed in the response word. Encoding the profile string back
 * into that word makes the emulated readout report exactly the profile. */
static uint32_t
encode_chip_id(const char *s)
{
    unsigned int x = 0, y = 0, w = 0;
    const char *it = s;

    if (*it >= 'A' && *it <= 'Z') {
        x = (unsigned int) (*it++ - 'A');
    } else if (*it >= 'a' && *it <= 'z') {
        x = (unsigned int) (*it++ - 'a');
    } else {
        return 0;
    }

    while (*it >= '0' && *it <= '9') {
        y = 10 * y + (unsigned int) (*it++ - '0');
    }

    /* Skip the wafer prefix, whatever separator spelling it uses. */
    while (*it != '\0' && *it != 'W' && *it != 'w') ++it;
    if (*it != '\0') ++it;

    while (*it >= '0' && *it <= '9') {
        w = 10 * w + (unsigned int) (*it++ - '0');
    }

    return (uint32_t) (((w & 0xFFF) << 8) | ((y & 0xF) << 4) | ((x + 1) & 0xF));
}

static void
log_cmd(katherine_emu_t *emu, const uint8_t *cmd)
{
    size_t slot;

    if (emu->log_count == KATHERINE_EMU_LOG_CAP) {
        /* Keep the most recent commands: drop the oldest entry. */
        emu->log_head = (emu->log_head + 1) % KATHERINE_EMU_LOG_CAP;
        --emu->log_count;
    }

    slot                    = (emu->log_head + emu->log_count) % KATHERINE_EMU_LOG_CAP;
    emu->log[slot].opcode   = cmd[6];
    emu->log[slot].subindex = cmd[4];
    emu->log[slot].payload  = katherine_emu_load_le32(cmd);
    ++emu->log_count;
}

/* Queue a response datagram, released once the virtual clock reaches the
 * configured latency (plus the seeded jitter, plus any time the readout
 * spends applying the command). */
static void
queue_crd(katherine_emu_t *emu, const uint8_t *bytes, uint64_t apply_ns)
{
    uint64_t jitter = katherine_emu_prng_below(&emu->jitter_rng, emu->profile.ack_jitter_ns + 1);
    size_t slot;

    if (emu->crd_count == KATHERINE_EMU_CRD_QUEUE_CAP) {
        ++emu->crd_dropped;
        return;
    }

    slot                  = (emu->crd_head + emu->crd_count) % KATHERINE_EMU_CRD_QUEUE_CAP;
    emu->crd[slot].due_ns = emu->now_ns + emu->profile.ack_latency_ns + jitter + apply_ns;
    memcpy(emu->crd[slot].bytes, bytes, KATHERINE_EMU_CRD_SIZE);
    ++emu->crd_count;
}

/* The acknowledgement of the readout is an otherwise empty datagram with
 * the operation code of the request echoed in byte 6. Responses carrying
 * data use the same encoding for the code and fill in the payload bytes. */
static void
queue_ack(katherine_emu_t *emu, uint8_t opcode, uint64_t apply_ns)
{
    uint8_t crd[KATHERINE_EMU_CRD_SIZE] = {0};
    crd[6]                              = opcode;
    queue_crd(emu, crd, apply_ns);
}

static void
queue_float(katherine_emu_t *emu, uint8_t opcode, float value)
{
    uint8_t crd[KATHERINE_EMU_CRD_SIZE] = {0};
    crd[6]                              = opcode;
    store_float(crd, value);
    queue_crd(emu, crd, 0);
}

static void
queue_word(katherine_emu_t *emu, uint8_t opcode, uint8_t subindex, uint32_t value)
{
    uint8_t crd[KATHERINE_EMU_CRD_SIZE] = {0};
    crd[6]                              = opcode;
    crd[4]                              = subindex;
    katherine_emu_store_le(crd, value, 4);
    queue_crd(emu, crd, 0);
}

static void
queue_readout_status(katherine_emu_t *emu)
{
    uint8_t crd[KATHERINE_EMU_CRD_SIZE] = {0};
    uint64_t val                        = 0;

    val = INSERT(val, readout_status_crd, hw_type, (uint64_t) emu->profile.hw_type);
    val = INSERT(val, readout_status_crd, hw_revision, (uint64_t) emu->profile.hw_revision);
    val = INSERT(val, readout_status_crd, hw_serial_number, (uint64_t) emu->profile.serial);
    val = INSERT(val, readout_status_crd, fw_version, (uint64_t) emu->profile.fw_version);

    /* The status fields occupy bytes 0 to 5; byte 6 carries the code. */
    katherine_emu_store_le(crd, val, 6);
    crd[6] = CMD_TYPE_GET_READOUT_STATUS;
    queue_crd(emu, crd, 0);
}

static void
queue_comm_status(katherine_emu_t *emu)
{
    uint8_t crd[KATHERINE_EMU_CRD_SIZE] = {0};
    uint64_t val                        = 0;
    uint64_t rate;

    /* The library scales the reported rate by five, so the field counts
       five-byte units per second. The emulator has no line telemetry of
       its own and reports the configured shaping rate, which is what
       limits its stream; an unshaped stream reports zero. */
    rate = emu->profile.shape_bytes_per_s / 5;
    if (rate > 0xFF) rate = 0xFF;

    val = INSERT(val, comm_status_crd, comm_lines_mask, (uint64_t) KATHERINE_EMU_COMM_LINES_MASK);
    val = INSERT(val, comm_status_crd, total_data_rate, rate);
    val = INSERT(val, comm_status_crd, chip_detected_flag, (uint64_t) KATHERINE_EMU_CHIP_COUNT);

    katherine_emu_store_le(crd, val, 3);
    /* Byte 3 is the measuring status, which the library discards. */
    crd[3] = emu->stream.armed ? 1 : 0;
    crd[6] = CMD_TYPE_GET_COMMUNICATION_STATUS;
    queue_crd(emu, crd, 0);
}

/* Pixel configuration upload: after the command, the readout consumes raw
 * configuration words from the command socket and answers nothing until
 * it has seen all of them. Datagrams that look like commands are consumed
 * as data all the same, which is what makes the recovery path of an
 * interrupted upload -- a flood of filler commands -- work at all. */
static void
consume_px_config(katherine_emu_t *emu, size_t len)
{
    uint32_t words = (uint32_t) (len / 4);

    if (words >= emu->px_upload_words) {
        emu->px_upload_words  = 0;
        emu->px_upload_active = false;
        queue_ack(emu, CMD_TYPE_SET_ALL_PIXEL_CONFIG, 0);
    } else {
        emu->px_upload_words -= words;
    }
}

static void
handle_cmd(katherine_emu_t *emu, const uint8_t *cmd)
{
    const uint16_t opcode  = (uint16_t) (cmd[6] | (cmd[7] << 8));
    const uint8_t subindex = cmd[4];
    const uint32_t payload = katherine_emu_load_le32(cmd);

    log_cmd(emu, cmd);

    switch (opcode) {
    case CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB:
        emu->regs.acq_time_lsb = payload;
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_ACQUISITION_TIME_SETTING_MSB:
        emu->regs.acq_time_msb = payload;
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_BIAS_SETTINGS:
        emu->regs.bias = load_float(cmd);
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_ACQUISITION_START:
        /* Arms the data plane and answers nothing: the measurement data
           stream is the response to this command. */
        katherine_emu_stream_arm(emu, (uint8_t) (payload & 0x1));
        break;

    case CMD_TYPE_INTERNAL_DAC_SETTINGS:
        if (subindex < KATHERINE_EMU_DAC_COUNT) {
            emu->regs.dac[subindex] = (uint16_t) payload;
        }
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_SEQ_READOUT_START:
        /* Selects the readout chain. Not acknowledged. */
        emu->regs.readout_mode = (uint8_t) (payload & 0x1);
        break;

    case CMD_TYPE_ACQUISITION_STOP:
        /* Not acknowledged either; the abort is visible in the data
           stream instead. */
        katherine_emu_stream_stop(emu);
        break;

    case CMD_TYPE_HW_COMMAND_START:
        /* The sub-command number travels in byte 0. All of them,
           including the matrix reset (5) and the pixel register load (9)
           that follow a configuration upload, are acknowledged. */
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_SENSOR_REGISTER_SETTING:
        if (subindex < KATHERINE_EMU_SENSOR_REG_COUNT) {
            emu->regs.sensor_reg[subindex] = payload;
        }
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_ACQUISITION_MODE_SETTING:
        /* The library packs the fast oscillator flag into the top bit of
           byte 0, next to the mode. This mirrors the client's own encoding
           (katherine_set_acq_mode(), config.c) bit-for-bit, including its
           byte-0-bit-7 placement, which is off by one byte against the
           CD[8] the manuals (v0.008+) and the Gen2 readout firmware
           document. Moot either way until the GeneralConfig rework config.c
           documents: the real firmware only shadows this write into a
           register it never flushes to the sensor. */
        emu->regs.acq_mode = (uint8_t) (cmd[0] & 0x3F);
        emu->regs.fast_vco = (cmd[0] & 0x80) != 0;
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_ECHO_CHIP_ID:
        queue_word(emu, (uint8_t) opcode, 0, emu->chip_id_word);
        break;

    case CMD_TYPE_GET_BIAS_VOLTAGE:
        queue_float(emu, (uint8_t) opcode, emu->regs.bias);
        break;

    case CMD_TYPE_GET_BIAS_CURRENT:
        /* No current model: the emulated bias supply is unloaded. */
        queue_float(emu, (uint8_t) opcode, 0.0f);
        break;

    case CMD_TYPE_GET_ADC_VOLTAGE:
        /* Synthetic ramp across the channels, so that a caller can tell
           the channels apart. */
        queue_float(emu, (uint8_t) opcode, 0.125f * (float) (cmd[0] + 1));
        break;

    case CMD_TYPE_GET_BACK_READ_REGISTER:
        queue_word(emu, (uint8_t) opcode, 0,
            cmd[0] < KATHERINE_EMU_SENSOR_REG_COUNT ? emu->regs.sensor_reg[cmd[0]] : 0);
        break;

    case CMD_TYPE_SET_PIXEL_CONFIG:
        /* A single configuration word; the matrix contents are not
           modeled, only the protocol. */
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_GET_PIXEL_CONFIG: {
        /* Matrix and DAC back-read both compare equal. */
        uint8_t crd[KATHERINE_EMU_CRD_SIZE] = {0};
        crd[0]                              = 1;
        crd[1]                              = 1;
        crd[6]                              = (uint8_t) opcode;
        queue_crd(emu, crd, 0);
        break;
    }

    case CMD_TYPE_SET_ALL_PIXEL_CONFIG:
        emu->px_upload_active = true;
        emu->px_upload_words  = KATHERINE_EMU_PX_CONFIG_WORDS;
        emu->px_upload_chip   = cmd[0];
        break;

    case CMD_TYPE_NUMBER_OF_FRAMES:
        emu->regs.no_frames = payload;
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_GET_HW_READOUT_TEMPERATURE:
        queue_float(emu, (uint8_t) opcode, emu->profile.readout_temperature);
        break;

    case CMD_TYPE_LED_SETTINGS:
        memcpy(emu->regs.led, cmd, sizeof(emu->regs.led));
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_GET_READOUT_STATUS:
        queue_readout_status(emu);
        break;

    case CMD_TYPE_GET_COMMUNICATION_STATUS:
        queue_comm_status(emu);
        break;

    case CMD_TYPE_GET_SENSOR_TEMPERATURE:
        queue_float(emu, (uint8_t) opcode, emu->profile.sensor_temperature);
        break;

    case CMD_TYPE_DIGITAL_TEST:
        queue_word(emu, (uint8_t) opcode, 0, KATHERINE_EMU_DIGITAL_TEST_PASSES);
        break;

    case CMD_TYPE_ACQUISITION_SETUP:
        emu->regs.acq_setup[subindex % KATHERINE_EMU_ACQ_SETUP_WORDS] = payload;
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_INTERNAL_TRIGGER_GENERATOR:
        emu->regs.trigger_gen[subindex % KATHERINE_EMU_TRIGGER_GEN_WORDS] = payload;
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_TEST_PULSE_SETTING:
        emu->regs.tp_count       = (uint16_t) (cmd[0] | (cmd[1] << 8));
        emu->regs.tp_period_code = cmd[2];
        emu->regs.tp_phase       = cmd[3];
        emu->regs.tp_flags       = cmd[4];
        /* The readout applies the pulse registers before acknowledging,
           and spends about a second doing so. */
        queue_ack(emu, (uint8_t) opcode, KATHERINE_EMU_TP_APPLY_NS);
        break;

    case EMU_CMD_TYPE_TOA_CALIBRATION_START:
    case CMD_TYPE_TOA_CALIBRATION_SETUP:
        /* Acknowledged, no effect modeled. */
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_NUMBER_OF_TOKENS_SETTING:
        emu->regs.tokens = cmd[0];
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_INTERNAL_TDC_SETTINGS:
        emu->regs.tdc_setup = payload;
        queue_ack(emu, (uint8_t) opcode, 0);
        break;

    case CMD_TYPE_INTERFACE_SELECTION:
        /* Switches the transport of the readout and answers nothing. */
        break;

    default:
        /* The readout's command dispatcher has no default branch: an
           opcode it does not recognize gets no response at all. Only
           counted here, so a caller can detect the condition. */
        ++emu->unknown_cmds;
        break;
    }
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * Fill in the default emulated readout properties.
 * @param profile Profile to initialize
 */
void
katherine_emu_profile_defaults(katherine_emu_profile_t *profile)
{
    if (profile == NULL) return;

    memset(profile, 0, sizeof(*profile));

    profile->hw_type     = 0x01;
    profile->hw_revision = 0x01;
    profile->serial      = 1;
    profile->fw_version  = 1;

    memcpy(profile->chip_id, "A1-W0001", sizeof("A1-W0001"));

    profile->readout_temperature = 30.0f;
    profile->sensor_temperature  = 40.0f;

    profile->ack_latency_ns = 0;
    profile->ack_jitter_ns  = 0;

    profile->seed = 42;

    profile->shape_bytes_per_s = 0;

    profile->pattern        = KATHERINE_EMU_PATTERN_UNIFORM;
    profile->hits_per_frame = 1000;
    profile->lost_per_frame = 0;
}

/**
 * Initialize an emulated readout in memory provided by the caller.
 *
 * The emulator allocates nothing: the given storage holds all of its
 * state, and it is fully initialized here regardless of its previous
 * contents.
 *
 * @param emu Emulator to initialize
 * @param profile Properties of the readout, or NULL for the defaults
 * @return Error code.
 */
int
katherine_emu_init(katherine_emu_t *emu, const katherine_emu_profile_t *profile)
{
    if (emu == NULL) return -KATHERINE_E_INVAL;

    memset(emu, 0, sizeof(*emu));

    /* Every sensor register, including GeneralConfig, starts at zero here.
       The real readout firmware instead boots with GeneralConfig = 0x0058
       (Gray_count_en, AckCommand_en and Fast_lo_en all set) -- the
       provenance of the historical preset katherine_general_config_word()
       (command_interface.h) reproduces by pinning those same three bits on
       every write. A client that always writes GeneralConfig before first
       use, as katherine_configure() does, never observes the difference. */

    if (profile != NULL) {
        emu->profile = *profile;
    } else {
        katherine_emu_profile_defaults(&emu->profile);
    }

    /* The identifier is used as a string; a caller-supplied buffer that
       is not terminated must not be read past its end. */
    emu->profile.chip_id[KATHERINE_EMU_CHIP_ID_SIZE - 1] = '\0';
    emu->chip_id_word                                    = encode_chip_id(emu->profile.chip_id);

    emu->jitter_rng.state = emu->profile.seed;

    katherine_emu_stream_reset(emu);

    return 0;
}

/**
 * Finalize an emulated readout.
 *
 * Nothing is released: the emulator owns no memory, no sockets and no
 * threads. The state is cleared, so that use after finalization is inert
 * rather than plausible, and the storage is the caller's again.
 *
 * @param emu Emulator
 */
void
katherine_emu_fini(katherine_emu_t *emu)
{
    if (emu == NULL) return;

    memset(emu, 0, sizeof(*emu));
    emu->stream.stage = KATHERINE_EMU_STAGE_IDLE;
}

/**
 * Deliver one command datagram to the emulated readout.
 *
 * While a pixel configuration upload is in progress, the datagram is
 * consumed as configuration data instead, even if its contents would
 * otherwise parse as a command.
 *
 * @param emu Emulator
 * @param data Start of the datagram
 * @param len Length of the datagram in bytes
 * @return Error code.
 */
int
katherine_emu_cmd_in(katherine_emu_t *emu, const void *data, size_t len)
{
    if (emu == NULL || data == NULL) return -KATHERINE_E_INVAL;

    if (emu->px_upload_active) {
        consume_px_config(emu, len);
        return 0;
    }

    /* The readout reads a fixed eight bytes per command; a longer
       datagram is truncated and a shorter one is not a command. */
    if (len < KATHERINE_EMU_CMD_SIZE) return -KATHERINE_E_INVAL;

    handle_cmd(emu, (const uint8_t *) data);
    return 0;
}

/**
 * Retrieve one command response datagram, if any is due.
 * @param emu Emulator
 * @param crd8 Start of a buffer of `KATHERINE_EMU_CRD_SIZE` bytes
 * @param len Number of bytes written (optional)
 * @return Error code, or -KATHERINE_E_TIMEOUT if no response is due yet.
 */
int
katherine_emu_crd_out(katherine_emu_t *emu, void *crd8, size_t *len)
{
    if (emu == NULL || crd8 == NULL) return -KATHERINE_E_INVAL;

    if (emu->crd_count == 0) return -KATHERINE_E_TIMEOUT;
    if (emu->crd[emu->crd_head].due_ns > emu->now_ns) return -KATHERINE_E_TIMEOUT;

    memcpy(crd8, emu->crd[emu->crd_head].bytes, KATHERINE_EMU_CRD_SIZE);
    emu->crd_head = (emu->crd_head + 1) % KATHERINE_EMU_CRD_QUEUE_CAP;
    --emu->crd_count;

    if (len != NULL) *len = KATHERINE_EMU_CRD_SIZE;
    return 0;
}

/**
 * Advance the virtual clock of the emulated readout.
 * @param emu Emulator
 * @param ns Amount of time to pass, in nanoseconds
 */
void
katherine_emu_advance(katherine_emu_t *emu, uint64_t ns)
{
    if (emu == NULL) return;

    emu->now_ns += ns;
    katherine_emu_stream_advance(emu, ns);
}

/**
 * Read the virtual clock of the emulated readout.
 * @param emu Emulator
 * @return Time since creation, in nanoseconds.
 */
uint64_t
katherine_emu_now(const katherine_emu_t *emu)
{
    return emu == NULL ? 0 : emu->now_ns;
}

/**
 * Read and clear recorded command datagrams, oldest first.
 * @param emu Emulator
 * @param entries Start of an array of at least `max` entries
 * @param max Maximum number of entries to read
 * @return Number of entries read.
 */
size_t
katherine_emu_log_read(katherine_emu_t *emu, katherine_emu_log_entry_t *entries, size_t max)
{
    size_t count = 0;

    if (emu == NULL || entries == NULL) return 0;

    while (count < max && emu->log_count > 0) {
        entries[count++] = emu->log[emu->log_head];
        emu->log_head    = (emu->log_head + 1) % KATHERINE_EMU_LOG_CAP;
        --emu->log_count;
    }

    return count;
}

/**
 * Count command datagrams the emulated readout did not implement.
 * @param emu Emulator
 * @return Number of unhandled commands received since creation.
 */
uint64_t
katherine_emu_unknown_cmd_count(const katherine_emu_t *emu)
{
    return emu == NULL ? 0 : emu->unknown_cmds;
}

/**
 * Count responses the emulated readout could not queue.
 *
 * The response queue is bounded, as the send buffer of a datagram socket
 * is; a caller that stops collecting responses while commands keep
 * arriving loses the excess.
 *
 * @param emu Emulator
 * @return Number of responses dropped since initialization.
 */
uint64_t
katherine_emu_dropped_crd_count(const katherine_emu_t *emu)
{
    return emu == NULL ? 0 : emu->crd_dropped;
}
