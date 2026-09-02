/**
 * \file
 * \brief Protocol emulator of the Katherine readout.
 * \author Petr Mánek
 * \date 21.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>
#include <katherine/error.h>

/**
 * \defgroup katherine_emulator Emulator
 * \ingroup katherine_c_api
 * \brief An in-process readout, for tests and for running without hardware.
 */

/**
 * \addtogroup katherine_emulator
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

// The emulator is a pure state machine reproducing the wire behavior of a
// readout: it consumes command datagrams, produces command response
// datagrams (CRDs) and produces the measurement data (MD) stream. It owns
// no memory, no sockets and no threads, and it never reads the wall clock
// -- all timing is driven by katherine_emu_advance(). Consequently, one
// profile, one seed and one sequence of calls always yield the same bytes,
// which makes recorded streams comparable across runs and machines.
//
// Datagrams are handed over whole: katherine_emu_cmd_in() takes one
// command datagram, katherine_emu_crd_out() returns one CRD, and
// katherine_emu_data_out() returns whole 6-byte MDs. Transporting them
// (over sockets or otherwise) is up to the caller.

/** Size of a command response datagram in bytes. */
#define KATHERINE_EMU_CRD_SIZE     8

/** Size of a measurement datum in bytes. */
#define KATHERINE_EMU_MD_SIZE      6

/** Size of the profile chip identifier buffer, including the terminator. */
#define KATHERINE_EMU_CHIP_ID_SIZE 16

/**
 * Spatial distribution of the emulated pixel hits.
 */
typedef enum katherine_emu_pattern {
    KATHERINE_EMU_PATTERN_UNIFORM    = 0, ///< Coordinates drawn uniformly over the whole matrix
    KATHERINE_EMU_PATTERN_HOT_COLUMN = 1, ///< All hits in a single column, one per row
    KATHERINE_EMU_PATTERN_GRADIENT   = 2, ///< Hit density rising linearly with the x coordinate
} katherine_emu_pattern_t;

/**
 * Emulated readout properties.
 *
 * Initialize with katherine_emu_profile_defaults() and override the
 * fields of interest; the emulator copies the profile on initialization.
 */
typedef struct katherine_emu_profile {
    uint8_t hw_type;     ///< Hardware type reported by the readout status command (0x01 is the first generation)
    uint8_t hw_revision; ///< Hardware revision reported by the readout status command
    uint16_t serial;     ///< Hardware serial number reported by the readout status command
    uint16_t fw_version; ///< Firmware version reported by the readout status command

    char chip_id[KATHERINE_EMU_CHIP_ID_SIZE]; ///< Sensor chip identifier, in the `A1-W0001` notation

    float readout_temperature; ///< Temperature reported for the readout board, in Celsius
    float sensor_temperature;  ///< Temperature reported for the sensor chip, in Celsius

    uint64_t ack_latency_ns; ///< Virtual time between a command and its response
    uint64_t ack_jitter_ns;  ///< Upper bound of the pseudo-random addition to the latency

    uint64_t seed; ///< Seed of the pseudo-random generators (jitter and pixel data)

    uint64_t shape_bytes_per_s; ///< Token bucket rate limit of the measurement data stream, zero to disable shaping

    katherine_emu_pattern_t pattern; ///< Spatial distribution of the emulated hits
    uint32_t hits_per_frame;         ///< Number of pixel MDs emitted per frame
    uint32_t lost_per_frame;         ///< Number of hits reported lost per frame, zero to omit the lost pixel MD
} katherine_emu_profile_t;

/**
 * One command datagram as observed by the emulator.
 *
 * The three fields are those the command protocol varies: the operation
 * code (byte 6 of the datagram), its sub-index (byte 4, e.g. a DAC or a
 * sensor register number) and the little-endian payload word (bytes 0 to
 * 3). Recording them makes command sequences comparable between the
 * emulator and a capture of real traffic.
 */
typedef struct katherine_emu_log_entry {
    uint8_t opcode;   ///< Operation code, byte 6 of the datagram
    uint8_t subindex; ///< Sub-index of the operation, byte 4 of the datagram
    uint32_t payload; ///< Payload word, bytes 0 to 3 of the datagram
} katherine_emu_log_entry_t;

//
// IMPORTANT NOTICE:
//
// The declarations from here until katherine_emu_t are internal state of
// the emulator. They appear in this header for one reason only: so that
// the caller can provide the storage of a katherine_emu_t (on the stack,
// statically, or from an allocator of its choosing). Do not read or write
// these fields; they change without notice.

/** Internal: number of internal DAC registers of the sensor. */
#define KATHERINE_EMU_DAC_COUNT         18

/** Internal: number of configuration registers of the sensor. */
#define KATHERINE_EMU_SENSOR_REG_COUNT  12

/** Internal: number of setup words of the acquisition unit. */
#define KATHERINE_EMU_ACQ_SETUP_WORDS   8

/** Internal: number of setup words of the trigger generator. */
#define KATHERINE_EMU_TRIGGER_GEN_WORDS 8

/** Internal: capacity of the response queue, in datagrams. An unrecognized
 *  opcode gets no response (the readout firmware's command dispatcher has
 *  no default branch), so a pixel configuration recovery flood leaves no
 *  backlog here; the margin instead covers a legitimate burst, such as an
 *  all-DAC scan's several dozen replies. A queue that does overflow drops
 *  the excess and counts it, as a datagram transport would. */
#define KATHERINE_EMU_CRD_QUEUE_CAP     256

/** Internal: capacity of the command log, in entries. Holds a full
 *  configuration session together with one recovery flood; beyond that,
 *  the oldest entries are dropped. */
#define KATHERINE_EMU_LOG_CAP           2048

/** Internal: number of measurement data generated ahead of the consumer. */
#define KATHERINE_EMU_STAGE_MDS         1024

/** Internal: state of a pseudo-random generator. */
typedef struct katherine_emu_prng {
    uint64_t state; ///< Internal
} katherine_emu_prng_t;

/** Internal: register file of the emulated readout. Commands write here;
 *  the data plane samples the acquisition-relevant entries when armed. */
typedef struct katherine_emu_regs {
    uint16_t dac[KATHERINE_EMU_DAC_COUNT];                 ///< Internal
    uint32_t sensor_reg[KATHERINE_EMU_SENSOR_REG_COUNT];   ///< Internal
    uint32_t acq_setup[KATHERINE_EMU_ACQ_SETUP_WORDS];     ///< Internal
    uint32_t trigger_gen[KATHERINE_EMU_TRIGGER_GEN_WORDS]; ///< Internal

    float bias; ///< Internal

    uint32_t acq_time_lsb; ///< Internal: in units of 10 ns, as sent by the acquisition time commands
    uint32_t acq_time_msb; ///< Internal
    uint32_t no_frames;    ///< Internal

    uint8_t acq_mode;     ///< Internal: reaches the sensor only once flushed
    bool fast_vco;        ///< Internal: reaches the sensor only once flushed
    uint8_t readout_mode; ///< Internal

    uint8_t shadow_acq_mode; ///< Internal: written by the acquisition-mode command, pending a flush
    bool shadow_fast_vco;    ///< Internal

    uint32_t tokens;    ///< Internal
    uint32_t tdc_setup; ///< Internal
    uint8_t led[4];     ///< Internal

    uint16_t tp_count;      ///< Internal
    uint8_t tp_period_code; ///< Internal
    uint8_t tp_phase;       ///< Internal
    uint8_t tp_flags;       ///< Internal
} katherine_emu_regs_t;

/** Internal: one queued command response datagram, released once the
 *  virtual clock reaches its due time. */
typedef struct katherine_emu_crd {
    uint64_t due_ns;                       ///< Internal
    uint8_t bytes[KATHERINE_EMU_CRD_SIZE]; ///< Internal
} katherine_emu_crd_t;

/** Internal: position of the measurement data generator within a frame. */
typedef enum katherine_emu_stage {
    KATHERINE_EMU_STAGE_NEW_FRAME = 0, ///< Internal
    KATHERINE_EMU_STAGE_START_LSB,     ///< Internal
    KATHERINE_EMU_STAGE_START_MSB,     ///< Internal
    KATHERINE_EMU_STAGE_PIXELS,        ///< Internal
    KATHERINE_EMU_STAGE_END_LSB,       ///< Internal
    KATHERINE_EMU_STAGE_END_MSB,       ///< Internal
    KATHERINE_EMU_STAGE_LOST,          ///< Internal
    KATHERINE_EMU_STAGE_FINISHED,      ///< Internal
    KATHERINE_EMU_STAGE_IDLE,          ///< Internal
} katherine_emu_stage_t;

/** Internal: state of the measurement data generator. */
typedef struct katherine_emu_stream {
    bool armed;                  ///< Internal: an acquisition is in progress
    bool frame_active;           ///< Internal: a frame has been opened and not yet finished
    katherine_emu_stage_t stage; ///< Internal

    uint8_t readout_mode; ///< Internal: sampled from the acquisition start command
    uint8_t acq_mode;     ///< Internal: sampled from the register file when armed
    bool fast_vco;        ///< Internal

    uint32_t frames_total;  ///< Internal
    uint32_t frame_index;   ///< Internal
    uint64_t frame_open_ns; ///< Internal
    uint64_t frame_len_ns;  ///< Internal

    katherine_emu_pattern_t pattern; ///< Internal: hit distribution, from the profile
    uint32_t hits;                   ///< Internal: pixel MDs per frame, from the profile
    uint32_t lost;                   ///< Internal: lost hits reported per frame, from the profile
    uint32_t px_index;               ///< Internal: pixels emitted in the current frame
    bool offset_sent;                ///< Internal: timestamp offset MD already emitted at this hit
    uint8_t hot_column;              ///< Internal: column selected by the hot column pattern

    uint8_t buf[KATHERINE_EMU_STAGE_MDS * KATHERINE_EMU_MD_SIZE]; ///< Internal: MDs generated but not yet handed over
    size_t buf_len;                                               ///< Internal
    size_t buf_pos;                                               ///< Internal

    uint64_t tokens;     ///< Internal: token bucket of the rate shaper
    uint64_t token_frac; ///< Internal

    katherine_emu_prng_t rng; ///< Internal
} katherine_emu_stream_t;

/**
 * Emulated readout.
 *
 * The caller provides the storage and hands it to katherine_emu_init();
 * all fields are internal state and must not be accessed directly.
 */
typedef struct katherine_emu {
    katherine_emu_profile_t profile; ///< Internal
    uint32_t chip_id_word;           ///< Internal: profile chip identifier in the wire encoding

    uint64_t now_ns; ///< Internal: virtual clock

    katherine_emu_regs_t regs; ///< Internal

    uint32_t px_upload_words; ///< Internal: configuration words still to be consumed as data
    bool px_upload_active;    ///< Internal
    uint8_t px_upload_chip;   ///< Internal

    katherine_emu_crd_t crd[KATHERINE_EMU_CRD_QUEUE_CAP]; ///< Internal
    size_t crd_head;                                      ///< Internal
    size_t crd_count;                                     ///< Internal
    uint64_t crd_dropped;                                 ///< Internal

    katherine_emu_log_entry_t log[KATHERINE_EMU_LOG_CAP]; ///< Internal
    size_t log_head;                                      ///< Internal
    size_t log_count;                                     ///< Internal

    uint64_t unknown_cmds; ///< Internal

    katherine_emu_prng_t jitter_rng; ///< Internal

    katherine_emu_stream_t stream; ///< Internal
} katherine_emu_t;

KATHERINE_EXPORTED void
katherine_emu_profile_defaults(katherine_emu_profile_t *profile);

KATHERINE_EXPORTED katherine_error_t
katherine_emu_init(katherine_emu_t *emu, const katherine_emu_profile_t *profile);

KATHERINE_EXPORTED void
katherine_emu_fini(katherine_emu_t *emu);

KATHERINE_EXPORTED katherine_error_t
katherine_emu_cmd_in(katherine_emu_t *emu, const void *data, size_t len);

KATHERINE_EXPORTED katherine_error_t
katherine_emu_crd_out(katherine_emu_t *emu, void *crd8, size_t *len);

KATHERINE_EXPORTED katherine_error_t
katherine_emu_data_out(katherine_emu_t *emu, void *buf, size_t cap, size_t *len);

KATHERINE_EXPORTED void
katherine_emu_advance(katherine_emu_t *emu, uint64_t ns);

KATHERINE_EXPORTED uint64_t
katherine_emu_now(const katherine_emu_t *emu);

KATHERINE_EXPORTED size_t
katherine_emu_log_read(katherine_emu_t *emu, katherine_emu_log_entry_t *entries, size_t max);

KATHERINE_EXPORTED uint64_t
katherine_emu_unknown_cmd_count(const katherine_emu_t *emu);

KATHERINE_EXPORTED uint64_t
katherine_emu_dropped_crd_count(const katherine_emu_t *emu);

#ifdef __cplusplus
}
#endif

/** \} */
