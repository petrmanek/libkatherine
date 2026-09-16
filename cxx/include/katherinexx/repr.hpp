/**
 * \file
 * \brief operator<< overloads delegating to the C rendering functions.
 * \author Petr Mánek
 * \date 24.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <ostream>
#include <vector>

#include <katherine/katherine.h>

#include <katherinexx/acquisition.hpp>
#include <katherinexx/config.hpp>
#include <katherinexx/device.hpp>
#include <katherinexx/udp.hpp>

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

namespace katherine {
namespace detail {

/**
 * Renders *v by calling snprint (one of the katherine_*_snprint() family)
 * with the two-call size-then-write idiom: the first call sizes the
 * rendering, a std::vector<char> of exactly that size (plus the NUL the
 * second call also writes but this never streams) is heap-allocated for
 * it -- no fixed buffer, so this is equally correct for a one-line struct
 * and for katherine_config_t. std::vector rather than std::string because
 * this header (via katherinexx.hpp) is a usage requirement of the
 * cxx_std_11 interface target (cxx/CMakeLists.txt): vector::data() has
 * been writable since C++11, while the non-const std::string::data()
 * overload this same idiom would otherwise use is C++17. Every operator<<
 * in this header is a one-liner over this single helper, so a change to
 * the idiom (or to what a struct renders as) needs no touching up here.
 */
template<typename T, typename Fn>
std::ostream&
stream_snprint(std::ostream& os, const T *v, Fn snprint)
{
    int len = snprint(nullptr, 0, v);
    if (len < 0) {
        return os; // formatting error; nothing sensible to stream
    }

    std::vector<char> buf(static_cast<std::size_t>(len) + 1);
    snprint(buf.data(), buf.size(), v);

    return os.write(buf.data(), static_cast<std::streamsize>(len));
}

}
}

// katherine::trigger, katherine::tpx3::dacs, katherine::test_pulse_config,
// katherine::frame_info and katherine::tpx3::coord are `using` aliases of
// these same C struct types (see config.hpp, acquisition.hpp, px_config.hpp),
// not distinct types, so argument-dependent lookup for a value of one of
// those alias types resolves to the global-namespace overloads below --
// a katherine::-qualified overload would never be found, because ADL does
// not see through an alias to a namespace that was never part of the
// actual type. katherine::px_config likewise needs no overload of its
// own: it publicly inherits katherine_px_config_t, and ADL for a derived
// class's argument also searches the associated namespaces of its base
// classes, so the katherine_px_config_t overload below is found for it
// too.

/** Renders a katherine_tpx3_coord_t (via katherine_tpx3_coord_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_tpx3_coord_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_tpx3_coord_snprint);
}

/** Renders a katherine_px_f_toa_tot_t (via katherine_px_f_toa_tot_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_px_f_toa_tot_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_px_f_toa_tot_snprint);
}

/** Renders a katherine_px_toa_tot_t (via katherine_px_toa_tot_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_px_toa_tot_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_px_toa_tot_snprint);
}

/** Renders a katherine_px_f_toa_only_t (via katherine_px_f_toa_only_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_px_f_toa_only_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_px_f_toa_only_snprint);
}

/** Renders a katherine_px_toa_only_t (via katherine_px_toa_only_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_px_toa_only_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_px_toa_only_snprint);
}

/** Renders a katherine_px_f_event_count_itot_t (via katherine_px_f_event_count_itot_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_px_f_event_count_itot_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_px_f_event_count_itot_snprint);
}

/** Renders a katherine_px_event_count_itot_t (via katherine_px_event_count_itot_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_px_event_count_itot_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_px_event_count_itot_snprint);
}

/** Renders a katherine_trigger_t (via katherine_trigger_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_trigger_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_trigger_snprint);
}

/** Renders a katherine_test_pulse_config_t (via katherine_test_pulse_config_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_test_pulse_config_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_test_pulse_config_snprint);
}

/** Renders a katherine_tpx3_dacs_t (via katherine_tpx3_dacs_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_tpx3_dacs_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_tpx3_dacs_snprint);
}

/** Renders a katherine_px_config_t as its digest (via katherine_px_config_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_px_config_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_px_config_snprint);
}

/** Renders a katherine_config_t (via katherine_config_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_config_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_config_snprint);
}

/** Renders a katherine_frame_info_time_t (via katherine_frame_info_time_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_frame_info_time_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_frame_info_time_snprint);
}

/** Renders a katherine_frame_info_t (via katherine_frame_info_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_frame_info_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_frame_info_snprint);
}

/** Renders a katherine_acquisition_t (via katherine_acquisition_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_acquisition_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_acquisition_snprint);
}

/** Renders a katherine_readout_status_t (via katherine_readout_status_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_readout_status_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_readout_status_snprint);
}

/** Renders a katherine_comm_status_t (via katherine_comm_status_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_comm_status_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_comm_status_snprint);
}

/** Renders a katherine_udp_t (via katherine_udp_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_udp_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_udp_snprint);
}

/** Renders a katherine_device_t (via katherine_device_snprint()). */
inline std::ostream&
operator<<(std::ostream& os, const katherine_device_t& v)
{
    return katherine::detail::stream_snprint(os, &v, katherine_device_snprint);
}

// The enumerations, rendered by the katherine_str_* family rather than by
// snprint. They need no helper: each of those functions returns a statically
// allocated string, so the overload is a cast and a stream.
//
// These go in the namespaces the enumerations themselves live in, unlike the
// struct overloads above. Every enum class here is a distinct C++ type
// declared inside katherine:: or katherine::tpx3::, so ADL finds an overload
// there -- the opposite of the alias case above, where the actual type is the
// C struct and only a global-namespace overload is ever considered.
//
// This is also the whole of how an enumeration is meant to render: the
// katherine_str_* functions are deliberately not bound as named C++ functions
// of their own, since a caller who wants the text streams the value or reaches
// for the C function directly.

namespace katherine {
namespace tpx3 {

/** Renders a katherine::tpx3::phase (via katherine_str_phase()). */
inline std::ostream&
operator<<(std::ostream& os, phase v)
{
    return os << katherine_str_phase((katherine_tpx3_phase_t) v);
}

/** Renders a katherine::tpx3::freq (via katherine_str_freq()). */
inline std::ostream&
operator<<(std::ostream& os, freq v)
{
    return os << katherine_str_freq((katherine_tpx3_freq_t) v);
}

/** Renders a katherine::tpx3::px_mode (via katherine_str_px_mode()). */
inline std::ostream&
operator<<(std::ostream& os, px_mode v)
{
    return os << katherine_str_px_mode((katherine_tpx3_px_mode_t) v);
}

/** Renders a katherine::tpx3::readout_mode (via katherine_str_readout_mode()). */
inline std::ostream&
operator<<(std::ostream& os, readout_mode v)
{
    return os << katherine_str_readout_mode((katherine_tpx3_readout_mode_t) v);
}

} // namespace tpx3

/** Renders a katherine::polarity (via katherine_str_polarity()). */
inline std::ostream&
operator<<(std::ostream& os, polarity v)
{
    return os << katherine_str_polarity((katherine_polarity_t) v);
}

/** Renders a katherine::acq_state (via katherine_str_acquisition_state()). */
inline std::ostream&
operator<<(std::ostream& os, acq_state v)
{
    return os << katherine_str_acquisition_state((katherine_acquisition_state_t) v);
}

/** Renders a katherine::phase_correction (via katherine_str_phase_correction()). */
inline std::ostream&
operator<<(std::ostream& os, phase_correction v)
{
    return os << katherine_str_phase_correction((katherine_phase_correction_t) v);
}

}

namespace katherine {

// Real wrapper classes: each owns its C struct by value (not by
// inheritance) and exposes a const accessor, so a value of one of these
// types is never implicitly convertible to the corresponding C struct --
// unlike katherine::px_config above, these need their own overload, found
// by ADL in this namespace.

/** Renders a katherine::udp over its c_udp() accessor. */
inline std::ostream&
operator<<(std::ostream& os, const katherine::udp& v)
{
    return detail::stream_snprint(os, v.c_udp(), katherine_udp_snprint);
}

/** Renders a katherine::device over its c_dev() accessor. */
inline std::ostream&
operator<<(std::ostream& os, const katherine::device& v)
{
    return detail::stream_snprint(os, v.c_dev(), katherine_device_snprint);
}

/** Renders a katherine::config over its c_config() accessor. */
inline std::ostream&
operator<<(std::ostream& os, const katherine::config& v)
{
    return detail::stream_snprint(os, v.c_config(), katherine_config_snprint);
}

/**
 * Renders a katherine::base_acquisition over its c_acq() accessor.
 *
 * Declared on the base rather than on the acquisition<AcqMode> template, so
 * one overload serves every mode: ADL for a derived class also searches the
 * associated namespaces of its bases, the same rule that lets
 * katherine::px_config reach the katherine_px_config_t overload above.
 */
inline std::ostream&
operator<<(std::ostream& os, const katherine::base_acquisition& v)
{
    return detail::stream_snprint(os, v.c_acq(), katherine_acquisition_snprint);
}

}

/** \} */
