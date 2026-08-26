/**
 * @file
 * @brief Command datagram builder and chip-envelope table.
 * @author Petr Mánek
 * @date 25.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <katherine/error.h>

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/**
 * A command datagram of this protocol: exactly 8 bytes, of which b[6] is
 * always the opcode and b[7] is unused by this device generation. The
 * remaining bytes carry a sub-index, a chip index and/or a payload word,
 * each placed by one of the setters below rather than written directly.
 */
typedef struct katherine_cmd {
    uint8_t b[8]; ///< Wire bytes, in transmission order
} katherine_cmd_t;

/**
 * Begin a command.
 * @param opcode Command opcode, placed at b[6].
 * @return A command with the given opcode and every other byte zero.
 */
static inline katherine_cmd_t
katherine_cmd_create(uint8_t opcode)
{
    katherine_cmd_t cmd = {0};
    cmd.b[6]            = opcode;
    return cmd;
}

/**
 * Set the sub-index of a command that takes one, such as the DAC index of
 * CMD_TYPE_INTERNAL_DAC_SETTINGS or the register index of
 * CMD_TYPE_SENSOR_REGISTER_SETTING.
 * @param cmd Command to modify.
 * @param subidx Sub-index, placed at b[4].
 */
static inline void
katherine_cmd_payload_set_subidx(katherine_cmd_t *cmd, uint8_t subidx)
{
    cmd->b[4] = subidx;
}

/**
 * Set the payload word of a command.
 *
 * Bounded by construction: the parameter is exactly as wide as the field, so
 * there is no value to truncate and no byte past b[3] the store could reach.
 * Callers holding a wider integer go through
 * katherine_cmd_payload_set_i64(), which documents what it drops.
 *
 * @param cmd Command to modify.
 * @param value Payload, stored little-endian into b[0..3]. Every other byte
 *   of the command is left untouched.
 */
static inline void
katherine_cmd_payload_set_u32(katherine_cmd_t *cmd, uint32_t value)
{
    cmd->b[0] = (uint8_t) (value & 0xffU);
    cmd->b[1] = (uint8_t) ((value >> 8) & 0xffU);
    cmd->b[2] = (uint8_t) ((value >> 16) & 0xffU);
    cmd->b[3] = (uint8_t) ((value >> 24) & 0xffU);
}

/**
 * Set the payload word of a command from a signed value wider than the
 * field, as the settings and DAC commands carry.
 *
 * The payload is a fixed 4-byte field, so only the low 32 bits of value
 * ever reach the wire. A negative value contributes the two's complement
 * bit pattern of those low 32 bits, matching the plain 32-bit word the
 * receiving hardware register takes with no sign extension.
 *
 * @param cmd Command to modify.
 * @param value Payload, truncated to its low 32 bits.
 */
static inline void
katherine_cmd_payload_set_i64(katherine_cmd_t *cmd, int64_t value)
{
    katherine_cmd_payload_set_u32(cmd, (uint32_t) value);
}

/**
 * Set the payload word of a command whose payload is IEEE-754 rather than an
 * integer, such as CMD_TYPE_BIAS_SETTINGS.
 *
 * The bytes are moved by memcpy rather than read through a differently-typed
 * pointer, which would be a strict-aliasing violation. Requires
 * sizeof(float) == sizeof(uint32_t), true on every platform this library
 * targets.
 *
 * @param cmd Command to modify.
 * @param value Payload, stored into the same field
 *   katherine_cmd_payload_set_u32() writes.
 */
static inline void
katherine_cmd_payload_set_f32(katherine_cmd_t *cmd, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    katherine_cmd_payload_set_u32(cmd, bits);
}

/**
 * Chip-index slot of an opcode: the byte position within a katherine_cmd_t
 * that holds its chip index, biased by one so that zero -- the value every
 * opcode absent from KATHERINE_CHIP_ENVELOPE is left with -- means "this
 * opcode has no chip-index slot". Byte 0 is itself a valid position and so
 * cannot serve as that sentinel; the bias is what keeps the two apart.
 */
#define KATHERINE_CHIP_SLOT(byte)      ((uint8_t) ((byte) + 1))

/** Byte position of a chip-index slot, undoing KATHERINE_CHIP_SLOT(). */
#define KATHERINE_CHIP_SLOT_BYTE(slot) ((uint8_t) ((slot) - 1))

/** Slot of an opcode that carries no chip index at all. */
#define KATHERINE_CHIP_SLOT_NONE       ((uint8_t) 0)

/**
 * Chip-index slot of every opcode, indexed by the opcode itself.
 *
 * Read off the readout firmware's command dispatcher, matching every opcode
 * it recognizes against the byte its own chip-index argument comes from:
 *   - byte 5: 0x04 (internal DAC settings) and 0x08 (sensor register
 *     setting);
 *   - byte 1: 0x07 (HW command dispatch) and the two DAC/register back-read
 *     opcodes 0x0E and 0x0F. On 0x07 the firmware also reads the index 100
 *     as "all chips" rather than as a chip number;
 *   - byte 0: 0x11, 0x12, 0x14, 0x19 and 0x20, none of which otherwise use
 *     that byte, and 0x0B (echo chip identifier), whose byte 0 is
 *     overloaded: the firmware reads a value below 32 as a chip index and
 *     anything above as a UDP port to answer on, so only chip indices are
 *     placeable here.
 * The last two entries, 0x05 (sequential readout start) and 0x27
 * (calibration start), are not in that dispatcher's chip-index paths at
 * all; they are placed at byte 5 on the authority of a mature reference
 * implementation of this protocol, which sends them there. Worth
 * confirming against hardware before a per-chip API relies on them.
 *
 * An opcode absent from this table has no known chip-index position on the
 * wire; katherine_cmd_payload_set_chip() below rejects a nonzero chip for it instead of
 * guessing where to place one. Every current caller of this library passes
 * chip 0, for which the question does not arise -- this table is the seam a
 * future per-chip API builds on, not something any command needs yet.
 *
 * The table spans the opcode's whole 8-bit range, so a lookup is one load
 * with no search and no bounds test: 256 bytes of read-only data buy a
 * constant-time answer for every opcode, listed or not.
 */
static const uint8_t KATHERINE_CHIP_ENVELOPE[256] = {
    [0x04] = KATHERINE_CHIP_SLOT(5),
    [0x08] = KATHERINE_CHIP_SLOT(5),
    [0x07] = KATHERINE_CHIP_SLOT(1),
    [0x0E] = KATHERINE_CHIP_SLOT(1),
    [0x0F] = KATHERINE_CHIP_SLOT(1),
    [0x0B] = KATHERINE_CHIP_SLOT(0),
    [0x11] = KATHERINE_CHIP_SLOT(0),
    [0x12] = KATHERINE_CHIP_SLOT(0),
    [0x14] = KATHERINE_CHIP_SLOT(0),
    [0x19] = KATHERINE_CHIP_SLOT(0),
    [0x20] = KATHERINE_CHIP_SLOT(0),
    [0x05] = KATHERINE_CHIP_SLOT(5),
    [0x27] = KATHERINE_CHIP_SLOT(5),
};

/**
 * Set the chip index of a command, in whichever byte
 * KATHERINE_CHIP_ENVELOPE assigns to its opcode.
 *
 * An opcode with no slot accepts chip 0 -- there is nothing to place, so
 * nothing is written -- but rejects a nonzero index rather than dropping it,
 * which would be a wire-format error invisible to the caller.
 *
 * @param cmd Command to modify.
 * @param opcode Opcode of the command, as passed to katherine_cmd_create().
 * @param chip Chip index to place.
 * @return 0 on success, -KATHERINE_E_BAD_CHIP for a nonzero chip index on an
 *   opcode that has no slot for one.
 */
static inline int
katherine_cmd_payload_set_chip(katherine_cmd_t *cmd, uint8_t opcode, uint8_t chip)
{
    const uint8_t slot = KATHERINE_CHIP_ENVELOPE[opcode];

    if (slot == KATHERINE_CHIP_SLOT_NONE) {
        if (chip != 0) return -KATHERINE_E_BAD_CHIP;
        return 0;
    }

    cmd->b[KATHERINE_CHIP_SLOT_BYTE(slot)] = chip;
    return 0;
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
