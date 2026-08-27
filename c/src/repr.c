/**
 * @file
 * @brief Debug stringification: katherine_*_snprint() and katherine_str_*().
 * @author Petr Mánek
 * @date 24.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <katherine/katherine.h>

/* This is the single source of truth for the human-readable rendering of
 * every printable public struct: one katherine_<name>_snprint() per type,
 * with exact snprintf() semantics (would-be length returned, safe
 * truncation, buf may be NULL when cap is 0), and one katherine_str_<enum>()
 * per printable enum. The C++ wrapper's operator<< overloads and the Python
 * bindings' __repr__ methods both delegate to these instead of reimplementing
 * any formatting, so all three surfaces render byte-identical strings.
 *
 * The output is always a single line: "<name>{field: value, field: value}",
 * where <name> is the struct name with the katherine_ prefix and _t suffix
 * stripped. A struct nested inside another (e.g. katherine_config_t's
 * dacs field) is rendered by calling its own snprint function, so a
 * change to one type's format is visible everywhere it nests.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/* Internal: given a growing buffer buf of capacity cap and a byte offset
 * off already accounted for by earlier appends (some possibly already
 * truncated, i.e. off may exceed cap), computes the destination pointer and
 * remaining room for the next append: NULL/0 once off has reached cap. This
 * is the one piece of overflow-clamp arithmetic every composing snprint
 * function below shares, through the two macros built on it.
 */
static inline void
repr_cursor(char *buf, size_t cap, size_t off, char **dst, size_t *rem)
{
    *rem = cap > off ? cap - off : 0;
    *dst = *rem > 0 ? buf + off : NULL;
}

/* Appends formatted text at the current offset, exactly like one more
 * snprintf() call continuing the same string: off is advanced by the number
 * of bytes the append would have produced given unlimited room (snprintf()'s
 * own return value), so that after the last of a sequence of REPR_APPENDF /
 * REPR_NEST calls, off holds the true (possibly truncated) total length --
 * the same contract a single snprintf() call has, extended over the whole
 * composed string.
 */
#define REPR_APPENDF(buf, cap, off, ...) \
    do { \
        char *dst_; \
        size_t rem_; \
        repr_cursor((buf), (cap), (off), &dst_, &rem_); \
        (off) += (size_t) snprintf(dst_, rem_, __VA_ARGS__); \
    } while (0)

/* Appends the rendering of a nested katherine_*_t value, produced by
 * calling its own katherine_*_snprint() (fn) at the current offset -- the
 * same idiom as REPR_APPENDF above, except the appended text comes from
 * another snprint call instead of a format string. This is how every nested
 * struct (config's triggers/dacs/test_pulse_config, device's two udp
 * sessions, a pixel type's coord, ...) composes into its owner's string.
 */
#define REPR_NEST(buf, cap, off, fn, ...) \
    do { \
        char *dst_; \
        size_t rem_; \
        repr_cursor((buf), (cap), (off), &dst_, &rem_); \
        (off) += (size_t) fn(dst_, rem_, __VA_ARGS__); \
    } while (0)

/* 64-bit XOR fold of the pixel configuration word array: a fingerprint for
 * telling two matrices apart at a glance in a log line, not a checksum --
 * collisions are neither avoided nor detected. Consecutive words are paired
 * up (the earlier of the pair in the high half) into a uint64_t and XORed
 * together; the array size (16384) is fixed and even, so every word
 * participates in exactly one pair.
 */
static uint64_t
px_config_xor64(const katherine_px_config_t *v)
{
    const size_t n = sizeof(v->words) / sizeof(v->words[0]);
    uint64_t acc   = 0;

    for (size_t i = 0; i < n; i += 2) {
        acc ^= ((uint64_t) v->words[i] << 32) | (uint64_t) v->words[i + 1];
    }

    return acc;
}

/* Hand-formats "A.B.C.D:port" from a sockaddr_in, without inet_ntop() (which
 * Windows declares in a different header than POSIX does): sin_addr and
 * sin_port are named the same on both platforms via katherine/udp_nix.h and
 * katherine/udp_win.h, and are the only members katherine_udp_snprint()
 * needs. buf must be at least 22 bytes ("255.255.255.255:65535" plus the
 * NUL); the caller below sizes it generously.
 */
static void
format_ipv4_port(char *buf, size_t cap, const struct sockaddr_in *addr)
{
    uint32_t host_addr = ntohl(addr->sin_addr.s_addr);
    uint16_t host_port = ntohs(addr->sin_port);

    snprintf(buf, cap, "%u.%u.%u.%u:%u", (unsigned) ((host_addr >> 24) & 0xFFu), (unsigned) ((host_addr >> 16) & 0xFFu),
        (unsigned) ((host_addr >> 8) & 0xFFu), (unsigned) (host_addr & 0xFFu), (unsigned) host_port);
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * Get stable, lowercase description of a readout type.
 * @param type Readout type to describe
 * @return Null-terminated string. "unknown" for a value outside the enum.
 */
const char *
katherine_str_readout_type(katherine_readout_type_t type)
{
    switch (type) {
    case READOUT_SEQUENTIAL:  return "sequential";
    case READOUT_DATA_DRIVEN: return "data_driven";
    default:                  return "unknown";
    }
}

/**
 * Get stable, lowercase description of an acquisition mode.
 * @param mode Acquisition mode to describe
 * @return Null-terminated string. "unknown" for a value outside the enum.
 */
const char *
katherine_str_acquisition_mode(katherine_acquisition_mode_t mode)
{
    switch (mode) {
    case ACQUISITION_MODE_TOA_TOT:    return "toa_tot";
    case ACQUISITION_MODE_ONLY_TOA:   return "only_toa";
    case ACQUISITION_MODE_EVENT_ITOT: return "event_itot";
    default:                          return "unknown";
    }
}

/**
 * Get stable, lowercase description of a pixel clock phase.
 * @param phase Phase to describe
 * @return Null-terminated string. "unknown" for a value outside the enum.
 */
const char *
katherine_str_phase(katherine_phase_t phase)
{
    switch (phase) {
    case PHASE_1:  return "phase_1";
    case PHASE_2:  return "phase_2";
    case PHASE_4:  return "phase_4";
    case PHASE_8:  return "phase_8";
    case PHASE_16: return "phase_16";
    default:       return "unknown";
    }
}

/**
 * Get stable, lowercase description of a pixel clock frequency.
 * @param freq Frequency to describe
 * @return Null-terminated string. "unknown" for a value outside the enum.
 */
const char *
katherine_str_freq(katherine_freq_t freq)
{
    switch (freq) {
    case FREQ_20:  return "freq_20";
    case FREQ_40:  return "freq_40";
    case FREQ_80:  return "freq_80";
    case FREQ_160: return "freq_160";
    default:       return "unknown";
    }
}

/**
 * Render pixel coordinates.
 * @param buf Destination buffer, or NULL if cap is 0
 * @param cap Capacity of buf in bytes
 * @param v Value to render
 * @return The number of bytes the rendering would occupy excluding the terminating NUL, same as snprintf().
 */
int
katherine_coord_snprint(char *buf, size_t cap, const katherine_coord_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "coord{x: %u, y: %u}", (unsigned) v->x, (unsigned) v->y);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_px_f_toa_tot_snprint(char *buf, size_t cap, const katherine_px_f_toa_tot_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "px_f_toa_tot{coord: ");
    REPR_NEST(buf, cap, off, katherine_coord_snprint, &v->coord);
    REPR_APPENDF(
        buf, cap, off, ", ftoa: %u, toa: %llu, tot: %u}", (unsigned) v->ftoa, (unsigned long long) v->toa, (unsigned) v->tot);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_px_toa_tot_snprint(char *buf, size_t cap, const katherine_px_toa_tot_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "px_toa_tot{coord: ");
    REPR_NEST(buf, cap, off, katherine_coord_snprint, &v->coord);
    REPR_APPENDF(
        buf, cap, off, ", toa: %llu, hit_count: %u, tot: %u}", (unsigned long long) v->toa, (unsigned) v->hit_count, (unsigned) v->tot);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_px_f_toa_only_snprint(char *buf, size_t cap, const katherine_px_f_toa_only_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "px_f_toa_only{coord: ");
    REPR_NEST(buf, cap, off, katherine_coord_snprint, &v->coord);
    REPR_APPENDF(buf, cap, off, ", ftoa: %u, toa: %llu}", (unsigned) v->ftoa, (unsigned long long) v->toa);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_px_toa_only_snprint(char *buf, size_t cap, const katherine_px_toa_only_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "px_toa_only{coord: ");
    REPR_NEST(buf, cap, off, katherine_coord_snprint, &v->coord);
    REPR_APPENDF(buf, cap, off, ", toa: %llu, hit_count: %u}", (unsigned long long) v->toa, (unsigned) v->hit_count);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_px_f_event_itot_snprint(char *buf, size_t cap, const katherine_px_f_event_itot_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "px_f_event_itot{coord: ");
    REPR_NEST(buf, cap, off, katherine_coord_snprint, &v->coord);
    REPR_APPENDF(buf, cap, off, ", event_count: %u, integral_tot: %u}", (unsigned) v->event_count,
        (unsigned) v->integral_tot);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_px_event_itot_snprint(char *buf, size_t cap, const katherine_px_event_itot_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "px_event_itot{coord: ");
    REPR_NEST(buf, cap, off, katherine_coord_snprint, &v->coord);
    REPR_APPENDF(buf, cap, off, ", hit_count: %u, event_count: %u, integral_tot: %u}", (unsigned) v->hit_count,
        (unsigned) v->event_count, (unsigned) v->integral_tot);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_trigger_snprint(char *buf, size_t cap, const katherine_trigger_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "trigger{enabled: %s, channel: %d, use_falling_edge: %s}", katherine_str_bool(v->enabled),
        (int) v->channel, katherine_str_bool(v->use_falling_edge));
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_test_pulse_config_snprint(char *buf, size_t cap, const katherine_test_pulse_config_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "test_pulse_config{enabled: %s, digital_only: %s, external: %s, count: %u, period: %u, phase: %u}",
        katherine_str_bool(v->enabled), katherine_str_bool(v->digital_only), katherine_str_bool(v->external), (unsigned) v->count,
        (unsigned) v->period, (unsigned) v->phase);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_dacs_snprint(char *buf, size_t cap, const katherine_dacs_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off,
        "dacs{Ibias_Preamp_ON: %u, Ibias_Preamp_OFF: %u, VPReamp_NCAS: %u, Ibias_Ikrum: %u, Vfbk: %u, "
        "Vthreshold_fine: %u, Vthreshold_coarse: %u, Ibias_DiscS1_ON: %u, Ibias_DiscS1_OFF: %u, "
        "Ibias_DiscS2_ON: %u, Ibias_DiscS2_OFF: %u, Ibias_PixelDAC: %u, Ibias_TPbufferIn: %u, "
        "Ibias_TPbufferOut: %u, VTP_coarse: %u, VTP_fine: %u, Ibias_CP_PLL: %u, PLL_Vcntrl: %u}",
        (unsigned) v->named.Ibias_Preamp_ON, (unsigned) v->named.Ibias_Preamp_OFF, (unsigned) v->named.VPReamp_NCAS,
        (unsigned) v->named.Ibias_Ikrum, (unsigned) v->named.Vfbk, (unsigned) v->named.Vthreshold_fine,
        (unsigned) v->named.Vthreshold_coarse, (unsigned) v->named.Ibias_DiscS1_ON, (unsigned) v->named.Ibias_DiscS1_OFF,
        (unsigned) v->named.Ibias_DiscS2_ON, (unsigned) v->named.Ibias_DiscS2_OFF, (unsigned) v->named.Ibias_PixelDAC,
        (unsigned) v->named.Ibias_TPbufferIn, (unsigned) v->named.Ibias_TPbufferOut, (unsigned) v->named.VTP_coarse,
        (unsigned) v->named.VTP_fine, (unsigned) v->named.Ibias_CP_PLL, (unsigned) v->named.PLL_Vcntrl);
    return (int) off;
}

/**
 * Render a digest of the pixel configuration matrix (word count and a
 * 64-bit XOR fold, never the 16384 words themselves -- see px_config_xor64()
 * above for how the fold is computed).
 * @copydetails katherine_coord_snprint
 */
int
katherine_px_config_snprint(char *buf, size_t cap, const katherine_px_config_t *v)
{
    size_t off     = 0;
    const size_t n = sizeof(v->words) / sizeof(v->words[0]);
    REPR_APPENDF(buf, cap, off, "px_config{words: %zu, xor64: 0x%016llx}", n, (unsigned long long) px_config_xor64(v));
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_config_snprint(char *buf, size_t cap, const katherine_config_t *v)
{
    size_t off = 0;

    REPR_APPENDF(buf, cap, off, "config{pixel_config: ");
    REPR_NEST(buf, cap, off, katherine_px_config_snprint, &v->pixel_config);
    REPR_APPENDF(buf, cap, off, ", bias_id: %u, acq_time: %g, no_frames: %d, bias: %g, start_trigger: ", (unsigned) v->bias_id,
        v->acq_time, v->no_frames, (double) v->bias);
    REPR_NEST(buf, cap, off, katherine_trigger_snprint, &v->start_trigger);
    REPR_APPENDF(buf, cap, off, ", delayed_start: %s, stop_trigger: ", katherine_str_bool(v->delayed_start));
    REPR_NEST(buf, cap, off, katherine_trigger_snprint, &v->stop_trigger);
    REPR_APPENDF(buf, cap, off, ", gray_disable: %s, polarity_holes: %s, phase: %s, freq: %s, dacs: ",
        katherine_str_bool(v->gray_disable), katherine_str_bool(v->polarity_holes), katherine_str_phase(v->phase),
        katherine_str_freq(v->freq));
    REPR_NEST(buf, cap, off, katherine_dacs_snprint, &v->dacs);
    REPR_APPENDF(buf, cap, off, ", test_pulse_config: ");
    REPR_NEST(buf, cap, off, katherine_test_pulse_config_snprint, &v->test_pulse_config);
    REPR_APPENDF(buf, cap, off, "}");

    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_frame_info_time_snprint(char *buf, size_t cap, const katherine_frame_info_time_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "frame_info_time{d: %llu, msb: %u, lsb: %u}", (unsigned long long) v->d, (unsigned) v->b.msb,
        (unsigned) v->b.lsb);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_frame_info_snprint(char *buf, size_t cap, const katherine_frame_info_t *v)
{
    size_t off = 0;

    REPR_APPENDF(buf, cap, off, "frame_info{received_pixels: %llu, sent_pixels: %llu, lost_pixels: %llu, start_time: ",
        (unsigned long long) v->received_pixels, (unsigned long long) v->sent_pixels, (unsigned long long) v->lost_pixels);
    REPR_NEST(buf, cap, off, katherine_frame_info_time_snprint, &v->start_time);
    REPR_APPENDF(buf, cap, off, ", end_time: ");
    REPR_NEST(buf, cap, off, katherine_frame_info_time_snprint, &v->end_time);
    REPR_APPENDF(buf, cap, off, ", start_time_observed: %lld, end_time_observed: %lld, completed: %s}",
        (long long) v->start_time_observed, (long long) v->end_time_observed, katherine_str_bool(v->completed));

    return (int) off;
}

/**
 * Render an acquisition. Only the fields useful in a log line are shown:
 * pointers (device, user_ctx, the data buffers) and the handler table are
 * omitted, as is everything about buffer occupancy beyond the two
 * capacities (pixel_buffer_valid/_max_valid, the timing fields, ...).
 * @copydetails katherine_coord_snprint
 */
int
katherine_acquisition_snprint(char *buf, size_t cap, const katherine_acquisition_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off,
        "acquisition{state: %s, readout_mode: %s, acq_mode: %s, aborted: %s, requested_frames: %d, completed_frames: %d, "
        "dropped_measurement_data: %zu, truncated_measurement_data: %llu, md_buffer_size: %zu, pixel_buffer_size: %zu}",
        katherine_str_acquisition_status(v->state), katherine_str_readout_type((katherine_readout_type_t) v->readout_mode),
        katherine_str_acquisition_mode((katherine_acquisition_mode_t) v->acq_mode), katherine_str_bool(v->aborted),
        v->requested_frames, v->completed_frames, v->dropped_measurement_data,
        (unsigned long long) v->truncated_measurement_data, v->md_buffer_size, v->pixel_buffer_size);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_readout_status_snprint(char *buf, size_t cap, const katherine_readout_status_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "readout_status{hw_type: %d, hw_revision: %d, hw_serial_number: %d, fw_version: %d}", v->hw_type,
        v->hw_revision, v->hw_serial_number, v->fw_version);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_comm_status_snprint(char *buf, size_t cap, const katherine_comm_status_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "comm_status{comm_lines_mask: 0x%02x, data_rate: %u Mb/s, chip_detected: %s}",
        (unsigned) v->comm_lines_mask, (unsigned) v->data_rate, katherine_str_bool(v->chip_detected));
    return (int) off;
}

/**
 * Render a UDP session's endpoints, pin state and command-response
 * correlation state. The socket handle and the mutex are omitted: neither is
 * meaningful in a log line.
 * @copydetails katherine_coord_snprint
 */
int
katherine_udp_snprint(char *buf, size_t cap, const katherine_udp_t *v)
{
    char local[24];
    char remote[24];
    size_t off = 0;

    format_ipv4_port(local, sizeof(local), &v->addr_local);
    format_ipv4_port(remote, sizeof(remote), &v->addr_remote);

    REPR_APPENDF(buf, cap, off,
        "udp{local: %s, remote: %s, pinned: %s, strict_ack: %s, stray_command_responses: %llu, last_os_error: %d}",
        local, remote, katherine_str_bool(v->remote_pinned), katherine_str_bool(v->strict_ack),
        (unsigned long long) v->stray_command_responses, v->last_os_error);
    return (int) off;
}

/** @copydoc katherine_coord_snprint */
int
katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v)
{
    size_t off = 0;
    REPR_APPENDF(buf, cap, off, "device{control_socket: ");
    REPR_NEST(buf, cap, off, katherine_udp_snprint, &v->control_socket);
    REPR_APPENDF(buf, cap, off, ", data_socket: ");
    REPR_NEST(buf, cap, off, katherine_udp_snprint, &v->data_socket);
    REPR_APPENDF(buf, cap, off, "}");
    return (int) off;
}
