/**
 * \file
 * \brief Example: configure and perform data-driven acquisition.
 * \author Petr Mánek
 * \date 13.07.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <katherinexx/katherinexx.hpp>

static std::string remote_address{"udp://192.168.1.145"};
using mode = katherine::tpx3::acq::f_toa_tot;

// Set the following flag to inject test pulses into a small subset of
// pixels during the acquisition.
static constexpr bool test_pulse = false;

void
paint_test_pixels(katherine::tpx3::px_config& px_config)
{
    // Enable test pulses for pixels forming the letter 'P' (chosen because
    // it is asymmetric in both axes, making image orientation verifiable).
    static const char *shape[] = {
        "XXX.",
        "X..X",
        "XXX.",
        "X...",
        "X...",
    };
    static constexpr int n_rows = sizeof(shape) / sizeof(shape[0]);
    static constexpr int scale  = 5;         // each shape cell becomes a scale x scale block of pixels
    static constexpr int x0 = 120, y0 = 120; // bottom-left corner of the shape
    int n_painted = 0;

    int row = 0;
    for (const auto *line : shape) {
        for (int col = 0; line[col]; ++col) {
            if (line[col] != 'X') continue;

            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    // Rows of the shape are listed top-down while the Y axis
                    // grows upwards, hence the flipped row index.
                    const int x = x0 + scale * col + dx;
                    const int y = y0 + scale * (n_rows - 1 - row) + dy;

                    if (x < 0 || x > 255 || y < 0 || y > 255) {
                        std::cerr << "Test pulse pixel at X = " << x << ",\t Y = " << y
                                  << " is out of bounds, skipping." << std::endl;
                        continue;
                    }

                    const katherine::tpx3::coord coord{
                        static_cast<std::uint8_t>(x),
                        static_cast<std::uint8_t>(y),
                    };
                    px_config.set_test_bit(coord, true);
                    ++n_painted;
                }
            }
        }
        ++row;
    }

    std::cerr << "Test pulses enabled for " << n_painted << " pixels." << std::endl;
}

void
configure(katherine::tpx3::config& config)
{
    using namespace std::literals::chrono_literals;

    // For now, these constants are hard-coded.
    // This configuration will produce meaningful results only for: M7-W0005
    config.set_acq_time(10s);
    config.set_frame_count(1);
    // One entry per bias supply, addressed from 0. Configuring drives every
    // supply the readout reports, so a supply left unset is driven to zero
    // rather than left where it was.
    config.set_bias(0, 230); // V


    config.set_start_trigger(katherine::no_trigger);
    config.set_stop_trigger(katherine::no_trigger);

    config.set_readout_mode(katherine::tpx3::readout_mode::data_driven);
    config.set_px_mode(katherine::tpx3::px_mode::toa_tot);
    config.set_fast_vco_enable(true);

    config.set_clock_phase(katherine::tpx3::phase::p1);
    config.set_clock_frequency(katherine::tpx3::freq::f40);

    katherine::tpx3::dacs dacs{};
    dacs.named.Ibias_Preamp_ON   = 128;
    dacs.named.Ibias_Preamp_OFF  = 8;
    dacs.named.Vpreamp_NCAS      = 128;
    dacs.named.Ibias_Ikrum       = 15;
    dacs.named.Vfbk              = 164;
    dacs.named.Vthreshold_fine   = 476;
    dacs.named.Vthreshold_coarse = 8;
    dacs.named.Ibias_DiscS1_ON   = 100;
    dacs.named.Ibias_DiscS1_OFF  = 8;
    dacs.named.Ibias_DiscS2_ON   = 128;
    dacs.named.Ibias_DiscS2_OFF  = 8;
    dacs.named.Ibias_PixelDAC    = 128;
    dacs.named.Ibias_TPbufferIn  = 128;
    dacs.named.Ibias_TPbufferOut = 128;
    dacs.named.VTP_coarse        = 128;
    dacs.named.VTP_fine          = 256;
    dacs.named.Ibias_CP_PLL      = 128;
    dacs.named.PLL_Vcntrl        = 128;


    // Reported here rather than left to the handler around main's body, as
    // krun.c reports it inline: a missing calibration file is not a readout
    // that would not answer, and one message for both would name the wrong
    // thing.
    katherine::tpx3::px_config px_config;
    try {
        px_config = katherine::tpx3::load_bmc_file("chipconfig.bmc");
    } catch (const katherine::error& e) {
        std::cout << "Cannot load pixel configuration. Does the file exist?\n"
                  << "Reason: " << e.what() << '\n';
        std::exit(1);
    }

    // One chip's settings for every chip: this example has only the one
    // calibration to hand, and a readout with two chips still has to be
    // told about both.
    for (std::uint8_t i = 0; i < config.chip_count(); ++i) {
        config.set_gray_disable(i, false);
        config.set_polarity(i, katherine::polarity::electrons);
        config.set_dacs(i, dacs);
        config.set_pixels(i, px_config);
    }

    // Test pulses are disabled unless requested above. (No explicit setup
    // is needed: a value-initialized config keeps them off.)
    if (test_pulse) {
        // Pulse the analog front-end with amplitude given by the difference
        // of the two DACs: |128 * 5 mV - 352 * 2.5 mV| = 240 mV.
        for (std::uint8_t i = 0; i < config.chip_count(); ++i) {
            config.dacs(i).named.VTP_coarse = 128;
            config.dacs(i).named.VTP_fine   = 352;
        }

        katherine::tpx3::test_pulse_config tp{};
        tp.enabled      = true;
        tp.digital_only = false;
        tp.external     = false;
        tp.count        = 100;
        tp.period       = 6401; // clock cycles, ~160 us @ 40 MHz
        tp.phase        = 0;
        config.set_test_pulses(std::move(tp));

        for (std::uint8_t i = 0; i < config.chip_count(); ++i) paint_test_pixels(config.pixels(i));
    }
}

static std::uint64_t n_hits;

void
frame_started(int frame_idx)
{
    n_hits = 0;

    std::cerr << "Started frame " << frame_idx << "." << std::endl;
    std::cerr << "X\tY\tToA\tfToA\tToT" << std::endl;
}

void
frame_ended(int frame_idx, bool completed, const katherine_frame_info_t& info)
{
    const double recv_perc = 100. * info.received_pixels / info.sent_pixels;

    std::cerr << std::endl
              << std::endl;
    std::cerr << "Ended frame " << frame_idx << "." << std::endl;
    std::cerr << " - tpx3->katherine lost " << info.lost_pixels << " pixels" << std::endl
              << " - katherine->pc sent " << info.sent_pixels << " pixels" << std::endl
              << " - katherine->pc received " << info.received_pixels << " pixels (" << recv_perc << " %)" << std::endl
              << " - state: " << (completed ? "completed" : "not completed") << std::endl
              << " - start time: " << info.start_time.d << std::endl
              << " - end time: " << info.end_time.d << std::endl;
}

void
pixels_received(const mode::pixel_type *px, size_t count)
{
    n_hits += count;

    for (size_t i = 0; i < count; ++i) {
        std::cerr << (unsigned) px[i].coord.x << '\t'
                  << (unsigned) px[i].coord.y << '\t'
                  << (unsigned long long) px[i].timestamp << '\t'
                  << (unsigned) px[i].tot << std::endl;
    }
}

void
print_chip_id(katherine::device& device)
{
    std::string chip_id = device.chip_id(0);
    std::cerr << "Chip ID: " << chip_id << std::endl;
}

void
run_acquisition(katherine::device& dev, const katherine::tpx3::config& c)
{
    using namespace std::chrono;
    using namespace std::literals::chrono_literals;

    katherine::acquisition<mode> acq{dev, katherine::md_size * 34952533, sizeof(mode::pixel_type) * 65536, 500ms, 10s, true};

    acq.set_frame_started_handler(frame_started);
    acq.set_frame_ended_handler(frame_ended);
    acq.set_pixels_received_handler(pixels_received);

    acq.begin(c);
    std::cerr << "Acquisition started." << std::endl;

    auto tic = steady_clock::now();
    acq.read();
    auto toc = steady_clock::now();

    double duration = duration_cast<milliseconds>(toc - tic).count() / 1000.;
    std::cerr << std::endl;
    std::cerr << "Acquisition completed:" << std::endl
              << " - state: " << acq.state() << std::endl
              << " - received " << acq.completed_frames() << " complete frames" << std::endl
              << " - dropped " << acq.dropped_measurement_data() << " measurement data items" << std::endl
              << " - total hits: " << n_hits << std::endl
              << " - total duration: " << duration << " s" << std::endl
              << " - throughput: " << (n_hits / duration) << " hits/s" << std::endl;
}

int
main(int argc, char *argv[])
{
    // As krun.c: the first argument names the readout, and the default
    // above is what the example ships with.
    if (argc > 1) remote_address = argv[1];
    std::cout << "Using remote address: " << remote_address << '\n';

    // Every call below can throw, and this is a program a reader runs
    // before they have a readout answering. Uncaught, the first one ends
    // in std::terminate and a core file; kinfoxx and kdacxx both catch,
    // and the exit code matches krun.c's for the same failure.
    try {
        katherine::device device{remote_address};

        // After the open, not before: the per-chip settings are sized at
        // construction and the readout is what says how many chips there are.
        katherine::tpx3::config c{device.info().chip_count};
        configure(c);

        print_chip_id(device);
        run_acquisition(device, c);
    } catch (const katherine::error& e) {
        std::cout << "Cannot initialize device. Is the address correct?\n"
                  << "Reason: " << e.what() << '\n';
        return 6;
    }

    return 0;
}
