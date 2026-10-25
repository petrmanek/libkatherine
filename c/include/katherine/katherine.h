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
 * \par Discarding an answer
 * Every pointer this interface writes a result through may be **NULL**,
 * which issues the command and throws the answer away: a liveness check, or
 * a command whose effect on the readout is the point. Pointers it reads
 * from -- the device, the configuration, an address -- may not. The one
 * in-out pointer is the exception in both directions: the count
 * ::katherine_device_discover_udp() and ::katherine_device_discover_usb()
 * take carries the buffer's capacity in, so NULL earns
 * ::KATHERINE_E_INVAL rather than a discarded result.
 *
 * \par Threads
 * The data and control planes are independent links and may be driven from
 * different threads. A read loop holds the data link for its whole
 * duration; a control exchange takes the control link, and the transports
 * serialise their own callers, so two threads issuing commands to one
 * device interleave exchanges rather than corrupting one.
 *
 * \par
 * Slow control during a measurement is therefore legal, with one class of
 * exception that is about the hardware rather than about threads: an
 * inquiry needing the chip's slow-control lanes cannot be served while
 * those lanes carry hits, and the calls that need them --
 * ::katherine_get_sensor_temperature(),
 * ::katherine_tpx3_get_dac_monitor_voltage() and its plural -- refuse with
 * ::KATHERINE_E_STATE while an acquisition is open on the device.
 * Readout-level inquiries have no such limit.
 *
 * \par
 * ::katherine_acquisition_abort() is the one call meant to be made from a
 * thread other than the one inside ::katherine_acquisition_read(); that is
 * what ends a read loop that would otherwise wait out its timeout.
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
