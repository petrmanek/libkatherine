/* Katherine Control Library
 *
 * This file was created on 28.2.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include "bitfields.h"

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
