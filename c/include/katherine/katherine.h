/**
 * \file
 * \brief Main umbrella header.
 * \author Petr Mánek
 * \date 29.5.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \defgroup katherine_c_api C API
 * \brief Native C interface of the Katherine control library.
 *
 * \details
 * \par Indexing
 * Every index this interface exposes counts from **zero**: the chip index,
 * the bias supply, the pulse generator's channels, the timing channels, the
 * GPIO channels and the trigger source. The label printed on a readout's
 * case may be one higher. Resolving that is a user interface's business, not
 * this library's, and the reference implementation records the offset in its
 * own enumerators for the same reason.
 *
 * \par
 * One pair looks like an exception and is not.
 * ::KATHERINE_TDC_SOURCE_GPIO_0 is 0 while
 * ::KATHERINE_TRIGGER_SOURCE_GPIO_0 is 1. Both name GPIO channel zero; they
 * encode into different register fields, and the trigger field spends its
 * zero on the pulse generator's internal channel. They are distinct types,
 * so mixing them is a compile error rather than a wrong trigger.
 */

#pragma once

#include <katherine/version.h>
#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/acquisition.h>
#include <katherine/px_config.h>
#include <katherine/config.h>
#include <katherine/device.h>
#include <katherine/status.h>
#include <katherine/gpio.h>
#include <katherine/tdc.h>
#include <katherine/pulse.h>
#include <katherine/toa.h>
#include <katherine/transport.h>
