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
    timed_out   = ACQUISITION_TIMED_OUT,
    aborted     = ACQUISITION_ABORTED
};

enum class acq_mode: int {
    toa_tot     = ACQUISITION_MODE_TOA_TOT,
    only_toa    = ACQUISITION_MODE_ONLY_TOA,
    event_itot  = ACQUISITION_MODE_EVENT_ITOT
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
class acquisition {
    katherine_acquisition_t acq_;

public:
    using pixel_type                = typename AcqMode::pixel_type;

    using pixels_received_handler   = std::function<void(const pixel_type *, std::size_t)>;
    using frame_started_handler     = std::function<void(int)>;
    using frame_ended_handler       = std::function<void(int, bool, const katherine_frame_info_t *)>;

private:
    pixels_received_handler pixels_received_handler_;
    frame_started_handler frame_started_handler_;
    frame_ended_handler frame_ended_handler_;

    static void
    forward_pixels_received(void *user_ctx, const void *px, size_t count)
    {
        auto self       = reinterpret_cast<acquisition*>(user_ctx);
        auto derived_px = reinterpret_cast<const pixel_type*>(px);
        self->pixels_received_handler_(derived_px, count);
    }

    static void
    forward_frame_started(void *user_ctx, int frame_idx)
    {
        auto self = reinterpret_cast<acquisition*>(user_ctx);
        self->frame_started_handler_(frame_idx);
    }

    static void
    forward_frame_ended(void *user_ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
    {
        auto self = reinterpret_cast<acquisition*>(user_ctx);
        self->frame_ended_handler_(frame_idx, completed, info);
    }

public:
    acquisition(device& dev, std::size_t md_buffer_size, std::size_t pixel_buffer_size)
        :acq_{},
         pixels_received_handler_{},
         frame_started_handler_{},
         frame_ended_handler_{}
    {
        int res = katherine_acquisition_init(&acq_, dev.c_dev(), reinterpret_cast<void*>(this), md_buffer_size, pixel_buffer_size);
        if (res != 0) {
            throw katherine::system_error{res};
        }

        /* TODO: uncomment in C++2a */
        acq_.handlers = {
            /* .pixels_received = */ acquisition::forward_pixels_received,
            /* .frame_started = */ acquisition::forward_frame_started,
            /* .frame_ended = */ acquisition::forward_frame_ended
        };
    }

    virtual ~acquisition()
    {
        katherine_acquisition_fini(&acq_);
    }

    void
    set_pixels_received_handler(pixels_received_handler&& fn)
    {
        pixels_received_handler_ = std::move(fn);
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
            (katherine_acquisition_mode_t) AcqMode::mode, AcqMode::fast_vco_enabled);

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
    int requested_frames() const                    { return acq_.requested_frames; }
    int completed_frames() const                    { return acq_.completed_frames; }
    std::size_t dropped_measurement_data() const    { return acq_.dropped_measurement_data; }

};

static inline const char *
str_acq_state(acq_state state)
{
    return katherine_str_acquisition_status((char) state);
}

}
