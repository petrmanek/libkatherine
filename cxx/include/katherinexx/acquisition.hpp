/**
 * \file
 * \brief C++ wrapper for the data acquisition process.
 * \author Petr Mánek
 * \date 28.1.19
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <functional>

#include <katherine/acquisition.h>

#include <katherinexx/device.hpp>
#include <katherinexx/config.hpp>
#include <katherinexx/error.hpp>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

namespace tpx3 {

/// Size of one measurement-data word, in bytes. A Timepix4 readout sends eight.
constexpr std::size_t md_size = KATHERINE_TPX3_MD_SIZE;

} // namespace tpx3

/// Where an acquisition has got to.
enum class acq_state {
    not_started = KATHERINE_ACQUISITION_STATE_NOT_STARTED,
    running     = KATHERINE_ACQUISITION_STATE_RUNNING,
    succeeded   = KATHERINE_ACQUISITION_STATE_SUCCEEDED,
    timed_out   = KATHERINE_ACQUISITION_STATE_TIMED_OUT
};

namespace tpx3 {

/**
 * What a katherine::tpx3::config::correct_clock_phase() request resolved to;
 * see katherine::base_acquisition::phase_correction().
 */
enum class phase_correction {
    none     = KATHERINE_TPX3_PHASE_CORRECTION_NONE,
    software = KATHERINE_TPX3_PHASE_CORRECTION_SOFTWARE,
    hardware = KATHERINE_TPX3_PHASE_CORRECTION_HARDWARE
};

} // namespace tpx3

// px_mode itself now lives in config.hpp (see the comment there); this
// header still uses it unqualified below via the katherinexx/config.hpp
// include above.

/// What one frame reported: its timestamps and how many hits of it arrived.
using frame_info = katherine_frame_info_t;

/**
 * The half of an acquisition that does not depend on what its pixels look
 * like: run, stop, report, and hand frame boundaries and raw data to the
 * caller.
 *
 * Not instantiated directly -- katherine::acquisition derives from it and
 * adds the one handler whose signature depends on the pixel type.
 */
class base_acquisition {
public:
    /// Called as each frame's shutter opens, with the frame's index.
    using frame_started_handler = std::function<void(std::uint32_t)>;
    /// Called as each frame ends, with whether it ended in order and what it reported.
    using frame_ended_handler = std::function<void(std::uint32_t, bool, const katherine::frame_info&)>;
    /// Called with measurement data as it arrives, undecoded.
    using data_received_handler = std::function<void(const char *, size_t)>;

protected:
    /// The C acquisition this drives; c_acq() is how a caller reaches it.
    katherine_acquisition_t acq_;

private:
    tpx3::px_mode mode_;
    bool fast_vco_enabled_;
    bool decode_data_;

    frame_started_handler frame_started_handler_;
    frame_ended_handler frame_ended_handler_;
    data_received_handler data_received_handler_;

    static void
    forward_frame_started(void *user_ctx, std::uint32_t frame_idx)
    {
        auto self = reinterpret_cast<base_acquisition *>(user_ctx);
        self->frame_started_handler_(frame_idx);
    }

    static void
    forward_frame_ended(void *user_ctx, std::uint32_t frame_idx, bool completed, const katherine_frame_info_t *info)
    {
        auto self = reinterpret_cast<base_acquisition *>(user_ctx);
        self->frame_ended_handler_(frame_idx, completed, *info);
    }

    static void
    forward_data_received(void *user_ctx, const char *px, size_t count)
    {
        auto self = reinterpret_cast<base_acquisition *>(user_ctx);
        self->data_received_handler_(px, count);
    }

public:
    /**
     * Allocates the buffers and takes the timeouts. Touches no socket: an
     * acquisition is set up here and started by begin().
     *
     * \param dev Readout to acquire from, which must outlive this object.
     * \param md_buffer_size Bytes of measurement data read at a time.
     * \param pixel_buffer_size Bytes of decoded pixels held before the handler is called.
     * \param report_timeout Silence after which a partly filled pixel buffer is delivered anyway.
     * \param fail_timeout Time past the measurement's own duration after which it is declared timed out.
     * \param mode The measurement a pixel makes.
     * \param fast_vco_enabled True to run the fine oscillator.
     * \param decode_data False to receive raw measurement data instead of pixels.
     */
    template<typename Rep1, typename Period1, typename Rep2, typename Period2>
    base_acquisition(device& dev, std::size_t md_buffer_size, std::size_t pixel_buffer_size, std::chrono::duration<Rep1, Period1> report_timeout, std::chrono::duration<Rep2, Period2> fail_timeout, tpx3::px_mode mode, bool fast_vco_enabled, bool decode_data)
        : acq_{},
          mode_{mode},
          fast_vco_enabled_{fast_vco_enabled},
          decode_data_{decode_data},
          frame_started_handler_{[](std::uint32_t) { }},
          frame_ended_handler_{[](std::uint32_t, bool, const katherine::frame_info&) { }},
          data_received_handler_{[](const char *, size_t) { }}
    {
        using namespace std::chrono;

        katherine::detail::check(katherine_acquisition_init(&acq_, dev.c_dev(), reinterpret_cast<void *>(this), md_buffer_size, pixel_buffer_size, static_cast<std::uint32_t>(duration_cast<milliseconds>(report_timeout).count()), static_cast<std::uint32_t>(duration_cast<milliseconds>(fail_timeout).count())));

        // TODO: uncomment in C++2a
        acq_.handlers = {
            /* .pixels_received = */ nullptr,
            /* .frame_started = */ base_acquisition::forward_frame_started,
            /* .frame_ended = */ base_acquisition::forward_frame_ended,
            /* .data_received = */ base_acquisition::forward_data_received,
        };
    }

    /**
     * Neither copied nor moved: it owns a buffer, a data link, and a pointer
     * the C acquisition holds back to it.
     *
     * The constructor hands katherine_acquisition_init() `this` as the user
     * context, which is how the C callbacks find the handlers. A move leaves
     * that pointing at the address the object used to be at; a copy frees
     * the pixel buffer twice.
     */
    base_acquisition(const base_acquisition&)            = delete;
    base_acquisition& operator=(const base_acquisition&) = delete;

    virtual ~base_acquisition()
    {
        katherine_acquisition_fini(&acq_);
    }

    /// Takes ownership of what each frame's start is reported to.
    void
    set_frame_started_handler(frame_started_handler&& fn)
    {
        frame_started_handler_ = std::move(fn);
    }

    /// Takes ownership of what each frame's end is reported to.
    void
    set_frame_ended_handler(frame_ended_handler&& fn)
    {
        frame_ended_handler_ = std::move(fn);
    }

    /// Takes ownership of what raw measurement data is handed to.
    void
    set_data_received_handler(data_received_handler&& fn)
    {
        data_received_handler_ = std::move(fn);
    }

    /**
     * Configures the readout and opens the shutter.
     *
     * \param config The configuration to send before acquiring. It carries
     *   the readout mode, the pixel mode and the oscillator setting, which
     *   used to be arguments here.
     *
     * \throws katherine::error with KATHERINE_E_INVAL if the configuration
     *   asks for a pixel mode or an oscillator setting other than the one
     *   this object's template argument fixed. The hit type the handler is
     *   given comes from that argument and the stream's comes from the
     *   configuration, so the two disagreeing would reinterpret one hit
     *   type as another rather than fail.
     */
    void
    begin(const katherine::tpx3::config& config)
    {
        if (config.px_mode() != mode_ || config.fast_vco_enable() != fast_vco_enabled_) {
            katherine::detail::check(KATHERINE_E_INVAL);
        }

        katherine::detail::check(katherine_acquisition_begin(&acq_, config.c_config(), decode_data_));
    }

    /// Stops at once and discards what has not arrived.
    void
    abort()
    {
        katherine::detail::check(katherine_acquisition_abort(&acq_));
    }

    /// Asks the readout to finish the measurement in order, and waits for it.
    void
    stop()
    {
        katherine::detail::check(katherine_acquisition_stop(&acq_));
    }

    /// Receives and decodes until the acquisition ends, calling the handlers as it goes.
    void
    read()
    {
        katherine::detail::check(katherine_acquisition_read(&acq_));
    }

    /**
     * The wrapped C acquisition, for the calls this class does not cover.
     * Present because config, device and udp all expose theirs the same way;
     * without it this was the one wrapper whose C struct could not be reached,
     * which also put the operator<< repr.hpp carries for it out of reach.
     * \return Pointer to the wrapped acquisition, valid as long as this object.
     */
    katherine_acquisition_t *c_acq() { return &acq_; }

    /** \copydoc c_acq() */
    const katherine_acquisition_t *c_acq() const { return &acq_; }

    /// Where this acquisition has got to.
    acq_state state() const { return (acq_state) acq_.state; }
    /// True once abort() has been called on this acquisition.
    bool aborted() const { return acq_.aborted; }
    /// Frames the configuration asked for.
    std::uint32_t requested_frames() const { return acq_.requested_frames; }
    /// Frames that have ended so far.
    std::uint32_t completed_frames() const { return acq_.completed_frames; }
    /// Measurement data the decoder could not keep up with; see dropped_input() for the kernel's own count.
    std::size_t dropped_measurement_data() const { return acq_.dropped_measurement_data; }
    /// Measurement data that arrived cut short of a whole word.
    std::uint64_t truncated_measurement_data() const { return acq_.truncated_measurement_data; }
    /// Bytes taken off the link, decoded or not; the yardstick a loss measurement in data-driven mode has.
    std::uint64_t received_measurement_data() const { return acq_.received_measurement_data; }

    /**
     * Measurement data the OS dropped for want of room in its receive queue.
     *
     * Counted by the kernel, not by this library, and so distinct from
     * dropped_measurement_data(), which is what the decoder could not keep
     * up with. Throws KATHERINE_E_UNSUPPORTED on a link that keeps no such
     * queue, the USB one among them.
     */
    std::uint64_t
    dropped_input() const
    {
        std::uint64_t count = 0;
        katherine::detail::check(katherine_transport_get_dropped_input(&acq_.data_transport, &count));
        return count;
    }

    /// The OS-level code behind the data link's last failure; see katherine::device::last_os_error().
    int
    last_os_error() const
    {
        return katherine_transport_get_last_os_error(&acq_.data_transport);
    }

    /**
     * The limit a data read waits within before it reports a timeout.
     *
     * What katherine_acquisition_t::report_timeout_ms is measured in steps of,
     * so a report timeout shorter than this one is never observed sooner
     * than this one.
     */
    void
    set_timeout(std::chrono::milliseconds timeout)
    {
        katherine::detail::check(
            katherine_transport_set_timeout(&acq_.data_transport, (std::uint32_t) timeout.count()));
    }

    /// The fine-tick offset this pixel's double column needs, which the decoder has already applied.
    std::uint8_t timestamp_phase_offset(tpx3::coord coord) const { return katherine_tpx3_timestamp_phase_offset(&acq_, coord); }

    /// What katherine::tpx3::config::correct_clock_phase() actually resolved to for this acquisition.
    tpx3::phase_correction phase_correction() const { return (tpx3::phase_correction) acq_.phase_correction; }
};


namespace tpx3 {
namespace acq {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

/// Time of arrival and time over threshold, with the fine oscillator running.
struct f_toa_tot {
    /// A hit with a fine time of arrival and a time over threshold.
    using pixel_type = katherine_tpx3_px_f_toa_tot_t;
    /// What the pixel measures.
    static constexpr px_mode mode = px_mode::toa_tot;
    /// Whether the fine oscillator runs, which only the 40 MHz clock supports.
    static constexpr bool fast_vco_enabled = true;
};

/// Time of arrival and time over threshold, at the coarse clock alone.
struct toa_tot {
    /// A hit with a coarse time of arrival and a time over threshold.
    using pixel_type = katherine_tpx3_px_toa_tot_t;
    /// What the pixel measures.
    static constexpr px_mode mode = px_mode::toa_tot;
    /// Whether the fine oscillator runs, which only the 40 MHz clock supports.
    static constexpr bool fast_vco_enabled = false;
};

/// Time of arrival only, with the fine oscillator running.
struct f_toa_only {
    /// A hit with a fine time of arrival and nothing else.
    using pixel_type = katherine_tpx3_px_f_toa_only_t;
    /// What the pixel measures.
    static constexpr px_mode mode = px_mode::only_toa;
    /// Whether the fine oscillator runs, which only the 40 MHz clock supports.
    static constexpr bool fast_vco_enabled = true;
};

/// Time of arrival only, at the coarse clock alone.
struct toa_only {
    /// A hit with a coarse time of arrival and a hit count.
    using pixel_type = katherine_tpx3_px_toa_only_t;
    /// What the pixel measures.
    static constexpr px_mode mode = px_mode::only_toa;
    /// Whether the fine oscillator runs, which only the 40 MHz clock supports.
    static constexpr bool fast_vco_enabled = false;
};

/// Event count and integral time over threshold, with the fine oscillator running.
struct f_event_count_itot {
    /// A pixel's event count and its integrated time over threshold.
    using pixel_type = katherine_tpx3_px_f_event_count_itot_t;
    /// What the pixel measures.
    static constexpr px_mode mode = px_mode::event_count_itot;
    /// Whether the fine oscillator runs, which only the 40 MHz clock supports.
    static constexpr bool fast_vco_enabled = true;
};

/// Event count and integral time over threshold, at the coarse clock alone.
struct event_count_itot {
    /// A pixel's event count and its integrated time over threshold.
    using pixel_type = katherine_tpx3_px_event_count_itot_t;
    /// What the pixel measures.
    static constexpr px_mode mode = px_mode::event_count_itot;
    /// Whether the fine oscillator runs, which only the 40 MHz clock supports.
    static constexpr bool fast_vco_enabled = false;
};

/** \} */

} // namespace acq
} // namespace tpx3


/**
 * One acquisition in one measurement mode.
 *
 * The mode is a template argument rather than a runtime one -- one of the
 * six tags in katherine::tpx3::acq -- so the pixel type the handler receives is
 * fixed at compile time and a handler written for the wrong one does not
 * compile.
 */
template<typename AcqMode>
class acquisition: public base_acquisition {
public:
    /// The hit this mode produces, which the handler receives an array of.
    using pixel_type = typename AcqMode::pixel_type;
    /// Called with each run of decoded hits, and with nothing else.
    using pixels_received_handler = std::function<void(const pixel_type *, std::size_t)>;

private:
    pixels_received_handler pixels_received_handler_;

    static void
    forward_pixels_received(void *user_ctx, const void *px, size_t count)
    {
        auto self       = reinterpret_cast<acquisition *>(user_ctx);
        auto derived_px = reinterpret_cast<const pixel_type *>(px);
        self->pixels_received_handler_(derived_px, count);
    }

public:
    /**
     * As katherine::base_acquisition's constructor, with the mode taken from
     * the template argument rather than passed.
     *
     * \param dev Readout to acquire from, which must outlive this object.
     * \param md_buffer_size Bytes of measurement data read at a time.
     * \param pixel_buffer_size Bytes of decoded pixels held before the handler is called.
     * \param report_timeout Silence after which a partly filled pixel buffer is delivered anyway.
     * \param fail_timeout Time past the measurement's own duration after which it is declared timed out.
     * \param decode_data False to receive raw measurement data instead of pixels.
     */
    template<typename Rep1, typename Period1, typename Rep2, typename Period2>
    acquisition(device& dev, std::size_t md_buffer_size, std::size_t pixel_buffer_size, std::chrono::duration<Rep1, Period1> report_timeout, std::chrono::duration<Rep2, Period2> fail_timeout, bool decode_data)
        : base_acquisition{dev, md_buffer_size, pixel_buffer_size, report_timeout, fail_timeout, AcqMode::mode, AcqMode::fast_vco_enabled, decode_data},
          pixels_received_handler_{[](const pixel_type *, std::size_t) { }}
    {
        acq_.handlers.pixels_received = acquisition::forward_pixels_received;
    }

    /// Takes ownership of what each run of decoded hits is handed to.
    void
    set_pixels_received_handler(pixels_received_handler&& fn)
    {
        pixels_received_handler_ = std::move(fn);
    }
};

/** \} */

}
