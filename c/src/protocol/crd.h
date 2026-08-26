/**
 * @file
 * @brief Internal bitfield definitions for command response data.
 * @author Petr Mánek
 * @date 28.2.19
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include "acq/bitfields.h"

// Define bitfield: readout_status_crd

// Define field: hw_type
#define _BITS_readout_status_crd_hw_type_start          0
#define _BITS_readout_status_crd_hw_type_mask           MASK(8)
#define _BITS_readout_status_crd_hw_type_type           uint8_t

// Define field: hw_revision
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

// Define field: chip_detected_flag
#define _BITS_comm_status_crd_chip_detected_flag_start  16
#define _BITS_comm_status_crd_chip_detected_flag_mask   MASK(8)
#define _BITS_comm_status_crd_chip_detected_flag_type   uint8_t

#endif
