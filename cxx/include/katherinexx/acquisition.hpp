/* Katherine Control Library
 *
 * This file was created on 28.1.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

#include <string>
#include <functional>

#include <katherine/acquisition.h>

#include <katherinexx/device.hpp>
#include <katherinexx/config.hpp>

namespace katherine {

static constexpr std::size_t md_size = KATHERINE_MD_SIZE;

enum class readout_type {
    sequential  = READOUT_SEQUENTIAL,
    data_driven = READOUT_DATA_DRIVEN
};

enum class acq_state {
    not_started = ACQUISITION_NOT_STARTED,
    running     = ACQUISITION_RUNNING,
    succeeded   = ACQUISITION_SUCCEEDED,
    timed_out   = ACQUISITION_TIMED_OUT
};

enum class acq_mode: int {
    toa_tot     = ACQUISITION_MODE_TOA_TOT,
    only_toa    = ACQUISITION_MODE_ONLY_TOA,
    event_itot  = ACQUISITION_MODE_EVENT_ITOT
};

using frame_info = katherine_frame_info_t;

class base_acquisition {
public:
    using frame_started_handler     = std::function<void(int)>;
    using frame_ended_handler       = std::function<void(int, bool, const katherine::frame_info&)>;

protected:
    katherine_acquisition_t acq_;

private:
    acq_mode mode_;
    bool fast_vco_enabled_;

    frame_started_handler frame_started_handler_;
    frame_ended_handler frame_ended_handler_;

    static void
    forward_frame_started(void *user_ctx, int frame_idx)
    {
        auto self = reinterpret_cast<base_acquisition*>(user_ctx);
        self->frame_started_handler_(frame_idx);
    }

    static void
    forward_frame_ended(void *user_ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
    {
        auto self = reinterpret_cast<base_acquisition*>(user_ctx);
        self->frame_ended_handler_(frame_idx, completed, *info);
    }

public:
    template<typename Rep1, typename Period1, typename Rep2, typename Period2>
    base_acquisition(device& dev, std::size_t md_buffer_size, std::size_t pixel_buffer_size, std::chrono::duration<Rep1, Period1> report_timeout, std::chrono::duration<Rep2, Period2> fail_timeout, acq_mode mode, bool fast_vco_enabled)
        :acq_{},
         mode_{mode},
         fast_vco_enabled_{fast_vco_enabled},
         frame_started_handler_{[](int){ }},
         frame_ended_handler_{[](int, bool, const katherine::frame_info&){ }}
    {
        using namespace std::chrono;

        int res = katherine_acquisition_init(&acq_, dev.c_dev(), reinterpret_cast<void*>(this), md_buffer_size, pixel_buffer_size, duration_cast<milliseconds>(report_timeout).count(), duration_cast<milliseconds>(fail_timeout).count());
        if (res != 0) {
            throw katherine::system_error{res};
        }

        /* TODO: uncomment in C++2a */
        acq_.handlers = {
            /* .pixels_received = */ nullptr,
            /* .frame_started = */ base_acquisition::forward_frame_started,
            /* .frame_ended = */ base_acquisition::forward_frame_ended
        };
    }

    virtual ~base_acquisition()
    {
        katherine_acquisition_fini(&acq_);
    }

    void
    set_frame_started_handler(frame_started_handler&& fn)
    {
        frame_started_handler_ = std::move(fn);
    }

    void
    set_frame_ended_handler(frame_ended_handler&& fn)
    {
        frame_ended_handler_ = std::move(fn);
    }

    void
    begin(const katherine::config& config, katherine::readout_type readout_type)
    {
        int res = katherine_acquisition_begin(&acq_, config.c_config(), (char) readout_type,
            (katherine_acquisition_mode_t) mode_, fast_vco_enabled_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    abort()
    {
        int res = katherine_acquisition_abort(&acq_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    read()
    {
        int res = katherine_acquisition_read(&acq_);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    acq_state state() const                         { return (acq_state) acq_.state; }
    bool aborted() const                            { return acq_.aborted; }
    int requested_frames() const                    { return acq_.requested_frames; }
    int completed_frames() const                    { return acq_.completed_frames; }
    std::size_t dropped_measurement_data() const    { return acq_.dropped_measurement_data; }

};


namespace acq {

struct f_toa_tot {
    using pixel_type                        = katherine_px_f_toa_tot_t;
    static constexpr acq_mode mode          = acq_mode::toa_tot;
    static constexpr bool fast_vco_enabled  = true;
};

struct toa_tot {
    using pixel_type                        = katherine_px_toa_tot_t;
    static constexpr acq_mode mode          = acq_mode::toa_tot;
    static constexpr bool fast_vco_enabled  = false;
};

struct f_toa_only {
    using pixel_type                        = katherine_px_f_toa_only_t;
    static constexpr acq_mode mode          = acq_mode::only_toa;
    static constexpr bool fast_vco_enabled  = true;
};

struct toa_only {
    using pixel_type                        = katherine_px_toa_only_t;
    static constexpr acq_mode mode          = acq_mode::only_toa;
    static constexpr bool fast_vco_enabled  = false;
};

struct f_event_itot {
    using pixel_type                        = katherine_px_f_event_itot_t;
    static constexpr acq_mode mode          = acq_mode::event_itot;
    static constexpr bool fast_vco_enabled  = true;
};

struct event_itot {
    using pixel_type                        = katherine_px_event_itot_t;
    static constexpr acq_mode mode          = acq_mode::event_itot;
    static constexpr bool fast_vco_enabled  = false;
};

}


template<typename AcqMode>
class acquisition: public base_acquisition {
public:
    using pixel_type                = typename AcqMode::pixel_type;
    using pixels_received_handler   = std::function<void(const pixel_type *, std::size_t)>;

private:
    pixels_received_handler pixels_received_handler_;

    static void
    forward_pixels_received(void *user_ctx, const void *px, size_t count)
    {
        auto self       = reinterpret_cast<acquisition*>(user_ctx);
        auto derived_px = reinterpret_cast<const pixel_type*>(px);
        self->pixels_received_handler_(derived_px, count);
    }

public:
    template<typename Rep1, typename Period1, typename Rep2, typename Period2>
    acquisition(device& dev, std::size_t md_buffer_size, std::size_t pixel_buffer_size, std::chrono::duration<Rep1, Period1> report_timeout, std::chrono::duration<Rep2, Period2> fail_timeout)
        :base_acquisition{dev, md_buffer_size, pixel_buffer_size, report_timeout, fail_timeout, AcqMode::mode, AcqMode::fast_vco_enabled},
         pixels_received_handler_{[](const pixel_type *, std::size_t){ }}
    {
        acq_.handlers.pixels_received = acquisition::forward_pixels_received;
    }

    void
    set_pixels_received_handler(pixels_received_handler&& fn)
    {
        pixels_received_handler_ = std::move(fn);
    }

};

static inline const char *
str_acq_state(acq_state state)
{
    return katherine_str_acquisition_status((char) state);
}

}
