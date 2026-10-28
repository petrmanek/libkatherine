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

#include "kargs.hpp"

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
    // This configuration will produce meaningful results only for: I8-W0036

    // We will configure settings from the most general to the most specific.
    // First we start with settings common to all chips.
    config.set_frame_duration(10s);
    config.set_frame_count(1);
    config.set_bias(0, 150); // V

    // The trigger structures can be used to delay the beginning or the end
    // of an acquisition based on external signals, connected to GPIO ports.
    // Here we just disable all, so that data collection starts immediately
    // and stops after a set time elapses. katherine::no_trigger spells out
    // every field of a disarmed trigger, including the delay.
    config.set_start_trigger(katherine::no_trigger);
    config.set_stop_trigger(katherine::no_trigger);

    // Next we proceed to settings that only apply to Timepix3 chips.
    config.set_readout_mode(katherine::tpx3::readout_mode::data_driven);
    config.set_px_mode(katherine::tpx3::px_mode::toa_tot);
    config.set_fast_vco_enable(true);

    config.set_clock_frequency(katherine::tpx3::freq::f40);
    config.set_clock_phase(katherine::tpx3::phase::p1);

    // The DAC setpoints determine how signals are digitized in all pixels.
    // For instance, Vthreshold_coarse and _fine sets the threshold voltage.
    katherine::tpx3::dacs dacs{};
    dacs.named.Ibias_Preamp_ON   = 128;
    dacs.named.Ibias_Preamp_OFF  = 8;
    dacs.named.Vpreamp_NCAS      = 128;
    dacs.named.Ibias_Ikrum       = 15;
    dacs.named.Vfbk              = 164;
    dacs.named.Vthreshold_fine   = 367;
    dacs.named.Vthreshold_coarse = 7;
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


    // Now we load values that will be set into registers of individual pixels.
    // These are typically kept in a binary file, which needs to be supplied
    // for each chip individually after chips are equalized. Alternatively,
    // libkatherine also provides a number of functions to create these values
    // from scratch in memory, or manipulate individual pixels (e.g. for tests).
    //
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

    // And now we configure the first Timepix3 chip specifically. If the
    // readout carries more chips than one, the following block has to be
    // repeated for each chip.
    config.set_gray_disable(0, false);
    config.set_polarity(0, katherine::polarity::holes);
    config.set_dacs(0, dacs);
    config.set_pixels(0, px_config);

    // Test pulses are disabled unless requested above. Even though a
    // value-initialized config keeps them off, it is a good practice not to
    // rely on that.
    config.set_test_pulses(katherine::tpx3::test_pulse_config{});

    if (test_pulse) {
        // If enabled, they are generated in a common way for all chips. Below
        // we will see, however, that each chip can define its own test-pulse
        // voltage using the "VTP" DAC setting.
        katherine::tpx3::test_pulse_config tp{};
        tp.enabled      = true;
        tp.digital_only = false;
        tp.external     = false;
        tp.count        = 100;
        tp.period       = 6401; // clock cycles, ~160 us @ 40 MHz
        tp.phase        = 0;
        config.set_test_pulses(std::move(tp));

        // Pulse the analog front-end with amplitude given by the difference
        // of the two DACs: |128 * 5 mV - 352 * 2.5 mV| = 240 mV.
        config.dacs(0).named.VTP_coarse = 128;
        config.dacs(0).named.VTP_fine   = 352;

        // And of course, each pixel in the chip gets a "test" bit which determines
        // whether test pulses are injected into it or not. We can demonstrate this
        // by flipping this bit in a specific subset of pixels to paint a recognizable
        // picture.
        paint_test_pixels(config.pixels(0));
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
              << " - start time: " << info.start_time[0].d << std::endl
              << " - end time: " << info.end_time[0].d << std::endl;

    // One tail per chip, and the chips' timers need not agree: on the
    // two-chip readout they sit a constant 55.4 ms apart. This example
    // configures a single chip, so there is only ever [0] to read; a
    // multi-chip program walks info.reporting_chips.
    if (info.reporting_chips > 1) {
        std::cout << " - note: " << unsigned{info.reporting_chips} << " chips reported, only the first is shown"
                  << std::endl;
    }
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

    katherine::acquisition<mode> acq{dev, katherine::tpx3::md_size * 34952533, sizeof(mode::pixel_type) * 65536, 500ms, 10s, true};

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

// To stdout for --help; this example has no usage error to report, every
// argument being an address.
static void
usage(std::ostream& out, const char *argv0)
{
    out << "Usage: " << argv0 << " [<address>]" << std::endl;
    out << "  e.g. " << argv0 << " udp://192.168.1.42" << std::endl;
    out << "  Runs one acquisition against the readout and reports its throughput." << std::endl;
    out << "  Needs chipconfig.bmc in the working directory." << std::endl;
    out << "  With no argument, uses " << remote_address << "." << std::endl;
}

int
main(int argc, char *argv[])
{
    if (kargs_wants_help(argc, argv)) {
        usage(std::cout, argv[0]);
        return 0;
    }

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

        // The configuration holds per-chip settings, and must correspond in
        // size to the number of detected chips. Here we assume exactly 1 chip
        // for the sake of simplicity, however in other applications users may
        // wish to retrieve the number of detected chips at runtime (see
        // device.info().chip_count) or use an adequate upper bound.
        const std::uint8_t expected_chip_count = 1;
        if (device.info().chip_count != expected_chip_count) {
            std::cout << "Refusing to proceed due to chip count mismatch: expected "
                      << unsigned{expected_chip_count} << ", but got " << unsigned{device.info().chip_count}
                      << " instead.\n";
            return 6;
        }

        katherine::tpx3::config config{expected_chip_count};
        configure(config);

        print_chip_id(device);
        run_acquisition(device, config);
    } catch (const katherine::error& e) {
        std::cout << "Cannot initialize device. Is the address correct?\n"
                  << "Reason: " << e.what() << '\n';
        return 6;
    }

    return 0;
}
