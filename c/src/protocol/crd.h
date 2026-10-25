/**
 * \file
 * \brief Internal bitfield definitions for command response data.
 * \author Petr Mánek
 * \date 28.2.19
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include "bitfields.h"

// Define bitfield: readout_status_crd

// Define field: hw_type
#define _BITS_readout_status_crd_hw_type_start          0
#define _BITS_readout_status_crd_hw_type_mask           MASK(8)
#define _BITS_readout_status_crd_hw_type_type           uint8_t

// Define field: hw_revision
//
// The whole byte, and on the second generation that is right: its firmware
// writes buffer_tmp[1] = HW_REV from `#define HW_REV 3`, a literal with
// nothing overlaid (ARM_APP/main.c getHWstatus(), :3859). The Gen2 manual
// §1.2.11 describes CRD[15] as a main-clock-source bit over this field;
// this firmware does not implement that, so there is no bit to mask here
// and no clock fact to recover.
//
// The first generation is where the bit is real, and only in its modern
// firmware. Disassembling getHWstatus() in both binaries under misc/fw/:
//
//   gen1_legacy  (fw 2)   revision byte = 3, a literal
//   gen1_modern  (fw 4)   revision byte = 3 | ((*TXP3_PORTS_CONTROLLER)[0x0C] & 1) << 7
//   gen2         (fw 5)   revision byte = 3, a literal
//
// So 0x83 from the modern Gen1 is revision 3 with a live hardware bit
// above it, read from bit 0 of the ports controller's register 0x0C --
// which is where the manual puts the main clock source. It is a state
// bit, not a variant marker: the same unit can report 0x03 or 0x83.
//
// Nothing keys on the revision, so this breaks nothing today; what it
// costs is that katherine_device_info_t::hw_revision is reported 128 too
// high whenever that bit is set, and the fact the bit carries is thrown
// away.
#define _BITS_readout_status_crd_hw_revision_start      8
#define _BITS_readout_status_crd_hw_revision_mask       MASK(8)
#define _BITS_readout_status_crd_hw_revision_type       uint8_t

// Define field: hw_serial_number
#define _BITS_readout_status_crd_hw_serial_number_start 16
#define _BITS_readout_status_crd_hw_serial_number_mask  MASK(16)
#define _BITS_readout_status_crd_hw_serial_number_type  uint16_t

// Define field: fw_version
#define _BITS_readout_status_crd_fw_version_start       32
#define _BITS_readout_status_crd_fw_version_mask        MASK(16)
#define _BITS_readout_status_crd_fw_version_type        uint16_t


// --------------------------------------------------------------------
// Define bitfield: comm_status_crd

// Define field: comm_lines_mask
#define _BITS_comm_status_crd_comm_lines_mask_start     0
#define _BITS_comm_status_crd_comm_lines_mask_mask      MASK(8)
#define _BITS_comm_status_crd_comm_lines_mask_type      uint8_t

// Define field: total_data_rate
#define _BITS_comm_status_crd_total_data_rate_start     8
#define _BITS_comm_status_crd_total_data_rate_mask      MASK(8)
#define _BITS_comm_status_crd_total_data_rate_type      uint8_t

// Define field: chip_count
#define _BITS_comm_status_crd_chip_count_start          16
#define _BITS_comm_status_crd_chip_count_mask           MASK(8)
#define _BITS_comm_status_crd_chip_count_type           uint8_t

#endif
