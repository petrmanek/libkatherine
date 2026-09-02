/**
 * \file
 * \brief 1.x source-compatibility shim.
 * \author Petr Mánek
 * \date 26.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <errno.h>
#include <katherine/error.h>

#include <katherine/katherine.h>

/**
 * \defgroup c_legacy_api Legacy C API
 * \brief Deprecated 1.x C interface, kept so that sources predating 2.0 keep
 *   compiling.
 *
 * \details
 * 2.0 replaced the return convention of every function in this group: each
 * used to return 0 on success and a `<errno.h>` value on failure, and now
 * returns 0 or a katherine_error_t enumerator (see katherine/error.h). This
 * header lets source written against 1.x keep compiling unmodified against
 * the 2.0 library: for each affected function, it defines a
 * `katherine1_<name>()` wrapper that calls the 2.0 function and translates
 * the result back to the 1.x convention, then renames the 1.x symbol onto
 * that wrapper with a function-like macro. Nothing here changes behavior
 * beyond the return value -- arguments, semantics and struct layouts are
 * exactly the 2.0 ones.
 *
 * Not every 1.x function needs a wrapper: `katherine_*_snprint()` (a
 * snprintf()-style length, never an error code), the `str_*()` lookups, and
 * the plain getters/setters of px_config.h never used the errno convention
 * and are unaffected. Functions with no 1.x history at all -- e.g.
 * katherine_udp_last_os_error(), katherine_strerror(), katherine_version()
 * -- have no 1.x name to preserve and so are not wrapped either.
 *
 * This header exports no new symbols of its own: every wrapper is `static
 * inline`, so linking 1.x source against the 2.0 library needs nothing this
 * header did not already provide as C source. The ABI break itself is
 * announced by the SONAME bump to libkatherine.so.2, not by anything here.
 *
 * Each wrapper also carries a deprecation attribute, so a consumer still
 * calling it under its 1.x name sees a compiler warning at its own call site
 * (see KATHERINE1_DEPRECATED in this header). This header, and the
 * compatibility it provides, is removed in libkatherine 3.0.
 *
 * The wrappers for katherine/emulator.h are declared only if that header was
 * already included before this one (KATHERINE_EMU_CRD_SIZE is defined):
 * emulator.h is not pulled in by katherine/katherine.h, and is not installed
 * at all unless the library was built with KATHERINE_BUILD_EMULATOR, so this
 * header cannot include it unconditionally itself. A consumer of the
 * emulator therefore has to include emulator.h *before* this header to get
 * 1.x return values from it; the other order compiles just as well but
 * leaves the emulator's own calls on the 2.0 convention, without a
 * diagnostic to say so.
 */

/**
 * \addtogroup c_legacy_api
 * \{
 */

//
// The #define renames below are ordered after every wrapper definition in
// this file on purpose: each wrapper's body calls the real 2.0 function by
// its plain name, and a rename macro in scope at that call would rewrite it
// into a call to the wrapper itself, recursing forever. Once the last
// wrapper is defined, nothing in this header calls a renamed name again, so
// the macros are safe to introduce from that point on -- including into any
// 1.x call site later in the same translation unit, which is the point of
// them.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#if defined(__GNUC__) || defined(__clang__)
#define KATHERINE1_DEPRECATED \
    __attribute__((deprecated("declared via katherine/katherine1.h, the 1.x compatibility shim; removed in libkatherine 3.0")))
#elif defined(_MSC_VER)
#define KATHERINE1_DEPRECATED \
    __declspec(deprecated("declared via katherine/katherine1.h, the 1.x compatibility shim; removed in libkatherine 3.0"))
#else
#define KATHERINE1_DEPRECATED
#endif

// Single point of translation from the 2.0 return convention (0, or a
// katherine_error_t enumerator) to the 1.x one (0, or a <errno.h> value);
// every wrapper below funnels its result through this rather than repeating
// the mapping.
//
// 1.x reported a failed address resolution or bind as EINVAL, not a
// dedicated code of its own (see katherine_udp_init_bound() as of the
// 1.1.0 release, the last one before this convention changed): both its
// local- and remote-address branches set `res = EINVAL` on an inet_pton()
// failure. KATHERINE_E_ADDR is the 2.0 enumerator that condition now maps
// to, hence the explicit case below rather than letting it fall to EIO.
//
// Every other katherine_error_t enumerator collapses onto EIO: 1.x had no
// single code for "malformed command response" or "unsupported by this
// device" either, since it had no error domain of its own to begin with,
// only whatever the failing syscall happened to set. A caller that needs
// the precise 2.0 cause should read the library's own return value with
// katherine_strerror() instead of going through this shim.
static inline int
katherine1_map_result(int result)
{
    if (result == 0) return 0;

    switch (result) {
    case KATHERINE_E_TIMEOUT: return ETIMEDOUT;
    case KATHERINE_E_NOMEM:   return ENOMEM;
    case KATHERINE_E_INVAL:   return EINVAL;
    case KATHERINE_E_ADDR:    return EINVAL;
    default:                  return EIO;
    }
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

// device.h

/** 1.x-named wrapper for katherine_device_init(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_device_init(katherine_device_t *device, const char *addr)
{
    return katherine1_map_result(katherine_device_init(device, addr));
}

// config.h

/** 1.x-named wrapper for katherine_dacs_validate(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_dacs_validate(const katherine_dacs_t *v)
{
    return katherine1_map_result(katherine_dacs_validate(v));
}

/** 1.x-named wrapper for katherine_configure(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_configure(katherine_device_t *device, const katherine_config_t *config)
{
    return katherine1_map_result(katherine_configure(device, config));
}

/** 1.x-named wrapper for katherine_set_all_pixel_config(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_all_pixel_config(katherine_device_t *device, const katherine_px_config_t *px_config)
{
    return katherine1_map_result(katherine_set_all_pixel_config(device, px_config));
}

/** 1.x-named wrapper for katherine_set_acq_time(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_acq_time(katherine_device_t *device, double ns)
{
    return katherine1_map_result(katherine_set_acq_time(device, ns));
}

/** 1.x-named wrapper for katherine_set_acq_mode(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_acq_mode(katherine_device_t *device, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled)
{
    return katherine1_map_result(katherine_set_acq_mode(device, acq_mode, fast_vco_enabled));
}

/** 1.x-named wrapper for katherine_set_no_frames(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_no_frames(katherine_device_t *device, int no_frames)
{
    return katherine1_map_result(katherine_set_no_frames(device, no_frames));
}

/** 1.x-named wrapper for katherine_set_bias(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_bias(katherine_device_t *device, unsigned char bias_id, float bias_value)
{
    return katherine1_map_result(katherine_set_bias(device, bias_id, bias_value));
}

/** 1.x-named wrapper for katherine_set_seq_readout_start(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_seq_readout_start(katherine_device_t *device, int arg)
{
    return katherine1_map_result(katherine_set_seq_readout_start(device, arg));
}

/** 1.x-named wrapper for katherine_acquisition_setup(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_acquisition_setup(katherine_device_t *device, const katherine_trigger_t *start_trigger, bool delayed_start, const katherine_trigger_t *end_trigger)
{
    return katherine1_map_result(katherine_acquisition_setup(device, start_trigger, delayed_start, end_trigger));
}

/** 1.x-named wrapper for katherine_set_sensor_register(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_sensor_register(katherine_device_t *device, char reg_idx, int32_t reg_value)
{
    return katherine1_map_result(katherine_set_sensor_register(device, reg_idx, reg_value));
}

/** 1.x-named wrapper for katherine_update_sensor_registers(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_update_sensor_registers(katherine_device_t *device)
{
    return katherine1_map_result(katherine_update_sensor_registers(device));
}

/** 1.x-named wrapper for katherine_output_block_config_update(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_output_block_config_update(katherine_device_t *device)
{
    return katherine1_map_result(katherine_output_block_config_update(device));
}

/** 1.x-named wrapper for katherine_timer_set(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_timer_set(katherine_device_t *device)
{
    return katherine1_map_result(katherine_timer_set(device));
}

/** 1.x-named wrapper for katherine_set_dacs(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_dacs(katherine_device_t *device, const katherine_dacs_t *dacs)
{
    return katherine1_map_result(katherine_set_dacs(device, dacs));
}

/** 1.x-named wrapper for katherine_set_test_pulses(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_set_test_pulses(katherine_device_t *device, const katherine_test_pulse_config_t *tp_config)
{
    return katherine1_map_result(katherine_set_test_pulses(device, tp_config));
}

// px_config.h

/** 1.x-named wrapper for katherine_px_config_load_bmc_file(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path)
{
    return katherine1_map_result(katherine_px_config_load_bmc_file(px_config, file_path));
}

/** 1.x-named wrapper for katherine_px_config_load_bmc_data(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_px_config_load_bmc_data(katherine_px_config_t *px_config, const katherine_bmc_t *bmc)
{
    return katherine1_map_result(katherine_px_config_load_bmc_data(px_config, bmc));
}

/** 1.x-named wrapper for katherine_px_config_load_bpc_file(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path)
{
    return katherine1_map_result(katherine_px_config_load_bpc_file(px_config, file_path));
}

/** 1.x-named wrapper for katherine_px_config_load_bpc_data(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_px_config_load_bpc_data(katherine_px_config_t *px_config, const katherine_bpc_t *bpc)
{
    return katherine1_map_result(katherine_px_config_load_bpc_data(px_config, bpc));
}

// status.h

/** 1.x-named wrapper for katherine_get_readout_status(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_get_readout_status(katherine_device_t *device, katherine_readout_status_t *status)
{
    return katherine1_map_result(katherine_get_readout_status(device, status));
}

/// 1.x read the readout's chip count as a boolean. The field is now the count
/// it always was, and an alias is safe here in a way the polarity one is not:
/// nonzero is truthy, so `if (status.chip_detected)` keeps meaning what it
/// meant. Aliasing a field re-pollutes the namespace for anyone who opts into
/// this header, which is what this header is for.
#define chip_detected chip_count

/** 1.x-named wrapper for katherine_get_comm_status(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_get_comm_status(katherine_device_t *device, katherine_comm_status_t *status)
{
    return katherine1_map_result(katherine_get_comm_status(device, status));
}

/** 1.x-named wrapper for katherine_get_chip_id(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_get_chip_id(katherine_device_t *device, char *s_chip_id)
{
    return katherine1_map_result(katherine_get_chip_id(device, s_chip_id));
}

/** 1.x-named wrapper for katherine_get_readout_temperature(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_get_readout_temperature(katherine_device_t *device, float *temperature)
{
    return katherine1_map_result(katherine_get_readout_temperature(device, temperature));
}

/** 1.x-named wrapper for katherine_get_sensor_temperature(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_get_sensor_temperature(katherine_device_t *device, float *temperature)
{
    return katherine1_map_result(katherine_get_sensor_temperature(device, temperature));
}

/** 1.x-named wrapper for katherine_perform_digital_test(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_perform_digital_test(katherine_device_t *device)
{
    return katherine1_map_result(katherine_perform_digital_test(device));
}

/** 1.x-named wrapper for katherine_get_adc_voltage(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_get_adc_voltage(katherine_device_t *device, unsigned char channel_id, float *voltage)
{
    return katherine1_map_result(katherine_get_adc_voltage(device, channel_id, voltage));
}

// acquisition.h

/** 1.x-named wrapper for katherine_acquisition_init(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout)
{
    return katherine1_map_result(katherine_acquisition_init(acq, device, ctx, md_buffer_size, pixel_buffer_size, report_timeout, fail_timeout));
}

/** 1.x-named wrapper for katherine_acquisition_begin(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_acquisition_begin(katherine_acquisition_t *acq, const katherine_config_t *config, char readout_mode, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled, bool decode_data)
{
    return katherine1_map_result(katherine_acquisition_begin(acq, config, readout_mode, acq_mode, fast_vco_enabled, decode_data));
}

/** 1.x-named wrapper for katherine_acquisition_abort(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_acquisition_abort(katherine_acquisition_t *acq)
{
    return katherine1_map_result(katherine_acquisition_abort(acq));
}

/** 1.x-named wrapper for katherine_acquisition_stop(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_acquisition_stop(katherine_acquisition_t *acq)
{
    return katherine1_map_result(katherine_acquisition_stop(acq));
}

/** 1.x-named wrapper for katherine_acquisition_read(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_acquisition_read(katherine_acquisition_t *acq)
{
    return katherine1_map_result(katherine_acquisition_read(acq));
}

// udp.h

/** 1.x-named wrapper for katherine_udp_init(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_init(katherine_udp_t *u, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
{
    return katherine1_map_result(katherine_udp_init(u, local_port, remote_addr, remote_port, timeout_ms));
}

/** 1.x-named wrapper for katherine_udp_init_bound(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
{
    return katherine1_map_result(katherine_udp_init_bound(u, local_addr, local_port, remote_addr, remote_port, timeout_ms));
}

/** 1.x-named wrapper for katherine_udp_send_exact(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
{
    return katherine1_map_result(katherine_udp_send_exact(u, data, count));
}

/** 1.x-named wrapper for katherine_udp_recv_exact(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_recv_exact(katherine_udp_t *u, void *data, size_t count)
{
    return katherine1_map_result(katherine_udp_recv_exact(u, data, count));
}

/** 1.x-named wrapper for katherine_udp_recv(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_recv(katherine_udp_t *u, void *data, size_t *count)
{
    return katherine1_map_result(katherine_udp_recv(u, data, count));
}

/** 1.x-named wrapper for katherine_udp_set_remote(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port)
{
    return katherine1_map_result(katherine_udp_set_remote(u, remote_addr, remote_port));
}

/** 1.x-named wrapper for katherine_udp_mutex_lock(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_mutex_lock(katherine_udp_t *u)
{
    return katherine1_map_result(katherine_udp_mutex_lock(u));
}

/** 1.x-named wrapper for katherine_udp_mutex_unlock(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_udp_mutex_unlock(katherine_udp_t *u)
{
    return katherine1_map_result(katherine_udp_mutex_unlock(u));
}

// emulator.h -- see the file header for why this is conditional.
#ifdef KATHERINE_EMU_CRD_SIZE

/** 1.x-named wrapper for katherine_emu_init(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_emu_init(katherine_emu_t *emu, const katherine_emu_profile_t *profile)
{
    return katherine1_map_result(katherine_emu_init(emu, profile));
}

/** 1.x-named wrapper for katherine_emu_cmd_in(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_emu_cmd_in(katherine_emu_t *emu, const void *data, size_t len)
{
    return katherine1_map_result(katherine_emu_cmd_in(emu, data, len));
}

/** 1.x-named wrapper for katherine_emu_crd_out(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_emu_crd_out(katherine_emu_t *emu, void *crd8, size_t *len)
{
    return katherine1_map_result(katherine_emu_crd_out(emu, crd8, len));
}

/** 1.x-named wrapper for katherine_emu_data_out(). */
KATHERINE1_DEPRECATED
static inline int
katherine1_emu_data_out(katherine_emu_t *emu, void *buf, size_t cap, size_t *len)
{
    return katherine1_map_result(katherine_emu_data_out(emu, buf, cap, len));
}

#endif /* KATHERINE_EMU_CRD_SIZE */

//
// Renames onto the wrappers above, in the 1.x names. See the file header
// for why these must come after every wrapper definition above rather than
// before.
#ifndef DOXYGEN_SHOULD_SKIP_THIS

#define katherine_device_init(...)                katherine1_device_init(__VA_ARGS__)

#define katherine_dacs_validate(...)              katherine1_dacs_validate(__VA_ARGS__)
#define katherine_configure(...)                  katherine1_configure(__VA_ARGS__)
#define katherine_set_all_pixel_config(...)       katherine1_set_all_pixel_config(__VA_ARGS__)
#define katherine_set_acq_time(...)               katherine1_set_acq_time(__VA_ARGS__)
#define katherine_set_acq_mode(...)               katherine1_set_acq_mode(__VA_ARGS__)
#define katherine_set_no_frames(...)              katherine1_set_no_frames(__VA_ARGS__)
#define katherine_set_bias(...)                   katherine1_set_bias(__VA_ARGS__)
#define katherine_set_seq_readout_start(...)      katherine1_set_seq_readout_start(__VA_ARGS__)
#define katherine_acquisition_setup(...)          katherine1_acquisition_setup(__VA_ARGS__)
#define katherine_set_sensor_register(...)        katherine1_set_sensor_register(__VA_ARGS__)
#define katherine_update_sensor_registers(...)    katherine1_update_sensor_registers(__VA_ARGS__)
#define katherine_output_block_config_update(...) katherine1_output_block_config_update(__VA_ARGS__)
#define katherine_timer_set(...)                  katherine1_timer_set(__VA_ARGS__)
#define katherine_set_dacs(...)                   katherine1_set_dacs(__VA_ARGS__)
#define katherine_set_test_pulses(...)            katherine1_set_test_pulses(__VA_ARGS__)

#define katherine_px_config_load_bmc_file(...)    katherine1_px_config_load_bmc_file(__VA_ARGS__)
#define katherine_px_config_load_bmc_data(...)    katherine1_px_config_load_bmc_data(__VA_ARGS__)
#define katherine_px_config_load_bpc_file(...)    katherine1_px_config_load_bpc_file(__VA_ARGS__)
#define katherine_px_config_load_bpc_data(...)    katherine1_px_config_load_bpc_data(__VA_ARGS__)

#define katherine_get_readout_status(...)         katherine1_get_readout_status(__VA_ARGS__)
#define katherine_get_comm_status(...)            katherine1_get_comm_status(__VA_ARGS__)
#define katherine_get_chip_id(...)                katherine1_get_chip_id(__VA_ARGS__)
#define katherine_get_readout_temperature(...)    katherine1_get_readout_temperature(__VA_ARGS__)
#define katherine_get_sensor_temperature(...)     katherine1_get_sensor_temperature(__VA_ARGS__)
#define katherine_perform_digital_test(...)       katherine1_perform_digital_test(__VA_ARGS__)
#define katherine_get_adc_voltage(...)            katherine1_get_adc_voltage(__VA_ARGS__)

#define katherine_acquisition_init(...)           katherine1_acquisition_init(__VA_ARGS__)
#define katherine_acquisition_begin(...)          katherine1_acquisition_begin(__VA_ARGS__)
#define katherine_acquisition_abort(...)          katherine1_acquisition_abort(__VA_ARGS__)
#define katherine_acquisition_stop(...)           katherine1_acquisition_stop(__VA_ARGS__)
#define katherine_acquisition_read(...)           katherine1_acquisition_read(__VA_ARGS__)

#define katherine_udp_init(...)                   katherine1_udp_init(__VA_ARGS__)
#define katherine_udp_init_bound(...)             katherine1_udp_init_bound(__VA_ARGS__)
#define katherine_udp_send_exact(...)             katherine1_udp_send_exact(__VA_ARGS__)
#define katherine_udp_recv_exact(...)             katherine1_udp_recv_exact(__VA_ARGS__)
#define katherine_udp_recv(...)                   katherine1_udp_recv(__VA_ARGS__)
#define katherine_udp_set_remote(...)             katherine1_udp_set_remote(__VA_ARGS__)
#define katherine_udp_mutex_lock(...)             katherine1_udp_mutex_lock(__VA_ARGS__)
#define katherine_udp_mutex_unlock(...)           katherine1_udp_mutex_unlock(__VA_ARGS__)

#ifdef KATHERINE_EMU_CRD_SIZE
#define katherine_emu_init(...)     katherine1_emu_init(__VA_ARGS__)
#define katherine_emu_cmd_in(...)   katherine1_emu_cmd_in(__VA_ARGS__)
#define katherine_emu_crd_out(...)  katherine1_emu_crd_out(__VA_ARGS__)
#define katherine_emu_data_out(...) katherine1_emu_data_out(__VA_ARGS__)
#endif /* KATHERINE_EMU_CRD_SIZE */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/** \} */
