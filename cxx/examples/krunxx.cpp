//
// Created by Petr Mánek on 13.07.18.
//

#include <chrono>
#include <iostream>

#include <katherinexx/katherinexx.hpp>

static const std::string remote_addr{"192.168.1.145"};
using mode = katherine::acq::f_toa_tot;

void
configure(katherine::config& config)
{
    using namespace std::literals::chrono_literals;

    // For now, these constants are hard-coded.
    // This configuration will produce meaningful results only for: M7-W0005
    config.set_bias_id(0);
    config.set_acq_time(10s);
    config.set_no_frames(1);
    config.set_bias(230); // V

    config.set_delayed_start(false);

    config.set_start_trigger(katherine::no_trigger);
    config.set_stop_trigger(katherine::no_trigger);

    config.set_gray_disable(false);
    config.set_polarity_holes(false);

    config.set_phase(katherine::phase::p1);
    config.set_freq(katherine::freq::f40);

    katherine::dacs dacs{};
    dacs.named.Ibias_Preamp_ON       = 128;
    dacs.named.Ibias_Preamp_OFF      = 8;
    dacs.named.VPReamp_NCAS          = 128;
    dacs.named.Ibias_Ikrum           = 15;
    dacs.named.Vfbk                  = 164;
    dacs.named.Vthreshold_fine       = 476;
    dacs.named.Vthreshold_coarse     = 8;
    dacs.named.Ibias_DiscS1_ON       = 100;
    dacs.named.Ibias_DiscS1_OFF      = 8;
    dacs.named.Ibias_DiscS2_ON       = 128;
    dacs.named.Ibias_DiscS2_OFF      = 8;
    dacs.named.Ibias_PixelDAC        = 128;
    dacs.named.Ibias_TPbufferIn      = 128;
    dacs.named.Ibias_TPbufferOut     = 128;
    dacs.named.VTP_coarse            = 128;
    dacs.named.VTP_fine              = 256;
    dacs.named.Ibias_CP_PLL          = 128;
    dacs.named.PLL_Vcntrl            = 128;
    config.set_dacs(std::move(dacs));

    katherine::px_config px_config = katherine::load_bmc_file("chipconfig.bmc");
    config.set_pixel_config(std::move(px_config));
}

static uint64_t n_hits;

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

    std::cerr << std::endl << std::endl;
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
                  << (unsigned) px[i].toa << '\t'
                  << (unsigned) px[i].ftoa << '\t'
                  << (unsigned) px[i].tot << std::endl;
    }
}

void
print_chip_id(katherine::device& device)
{
    std::string chip_id = device.chip_id();
    std::cerr << "Chip ID: " << chip_id << std::endl;
}

void
run_acquisition(katherine::device& dev, const katherine::config& c)
{
    using namespace std::chrono;
    using namespace std::literals::chrono_literals;

    katherine::acquisition<mode> acq{dev, katherine::md_size * 34952533, sizeof(mode::pixel_type) * 65536, 500ms, 10s, true};

    acq.set_frame_started_handler(frame_started);
    acq.set_frame_ended_handler(frame_ended);
    acq.set_pixels_received_handler(pixels_received);

    acq.begin(c, katherine::readout_type::data_driven);
    std::cerr << "Acquisition started." << std::endl;

    auto tic = steady_clock::now();
    acq.read();
    auto toc = steady_clock::now();

    double duration = duration_cast<milliseconds>(toc - tic).count() / 1000.;
    std::cerr << std::endl;
    std::cerr << "Acquisition completed:" << std::endl
                << " - state: " << katherine::str_acq_state(acq.state()) << std::endl
                << " - received " << acq.completed_frames() << " complete frames" << std::endl
                << " - dropped " << acq.dropped_measurement_data() << " measurement data items" << std::endl
                << " - total hits: " << n_hits << std::endl
                << " - total duration: " << duration << " s" << std::endl
                << " - throughput: " << (n_hits / duration) << " hits/s" << std::endl;
}

int
main(int argc, char *argv[])
{
    katherine::config c{};
    configure(c);

    katherine::device device{remote_addr};
    print_chip_id(device);
    run_acquisition(device, c);

    return 0;
}
