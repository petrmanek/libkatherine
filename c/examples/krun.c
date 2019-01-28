//
// Created by Petr Mánek on 13.07.18.
//

/*#include <chrono>
#include <iostream>
#include <mutex>
#include <yaml-cpp/yaml.h>
#include <katherine/katherine.h>

static std::mutex cerr_mutex;

void
print_chip_id(katherine_device_t *device)
{
    int res;
    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];

    res = katherine_get_chip_id(device, chip_id);
    if (res) {
        std::cerr << "Could not get response in time. Is the device on and connected?" << std::endl;
        std::abort();
    }

    std::cerr << "Chip ID: " << chip_id << std::endl;
}

static int frame_count;
static uint64_t n_hits;

void
frame_started(int frame_idx)
{
    n_hits = 0;

    {
        std::lock_guard<std::mutex> lk{cerr_mutex};
        std::cerr << "Started frame " << (frame_idx + 1) << " / " << frame_count << "." << std::endl;
        std::cerr << " - have pixels: " << 0 << std::flush;
    }
}

void
frame_ended(int frame_idx, bool completed, const katherine_frame_info_t *info)
{
    std::cout << std::endl;

    {
        std::lock_guard<std::mutex> lk{cerr_mutex};
        std::cerr << std::endl << std::endl;
        std::cerr << "Ended frame " << (frame_idx + 1) << " / " << frame_count << "." << std::endl;
        std::cerr << " - katherine->pc received " << info->received_pixels << " pixels" << std::endl
                  << " - katherine->pc sent " << info->sent_pixels << " pixels" << std::endl
                  << " - tpx3->katherine lost " << info->lost_pixels << " pixels" << std::endl
                  << " - state: " << (completed ? "completed" : "not completed") << std::endl
                  << " - start time: " << info->start_time.d << std::endl
                  << " - end time: " << info->end_time.d << std::endl;
    }
}

void
pixels_received(const void *pixels, size_t count)
{
    n_hits += count;

    {
        std::lock_guard<std::mutex> lk{cerr_mutex};
        std::cerr << "\r - have pixels: " << n_hits << std::flush;
    }

    const auto *px = (const katherine_px_f_toa_tot_t *) pixels;
    for (size_t i = 0; i < count; ++i) {
        std::cout << (unsigned) px[i].coord.x << ' ' << (unsigned) px[i].coord.y << ' '
                  << (unsigned) px[i].toa << ' ' << (unsigned) px[i].ftoa << ' '
                  << (unsigned) px[i].tot << std::endl;
    }
}

void
configure_yaml(katherine_config_t& config, const YAML::Node& yml)
{
    // For now, these three constants are hard-coded.
    config.seq_readout_start = true;
    config.fast_vco_enabled = true;
    config.acq_mode = ACQUISITION_MODE_TOA_TOT;

    config.bias_id = yml["bias_id"].as<unsigned char>();
    config.acq_time = yml["acq_time"].as<double>() * 1e9;
    config.no_frames = yml["no_frames"].as<int>();
    config.bias = yml["bias"].as<float>();

    frame_count = config.no_frames;

    config.delayed_start = yml["delayed_start"].as<bool>();

    auto yml_start = yml["start_trigger"].as<YAML::Node>();
    config.start_trigger.use_falling_edge = yml_start["use_falling_edge"].as<bool>();
    config.start_trigger.channel = yml_start["channel"].as<char>();
    config.start_trigger.enabled = yml_start["enabled"].as<bool>();

    auto yml_end = yml["end_trigger"].as<YAML::Node>();
    config.stop_trigger.use_falling_edge = yml_end["use_falling_edge"].as<bool>();
    config.stop_trigger.channel = yml_end["channel"].as<char>();
    config.stop_trigger.enabled = yml_end["enabled"].as<bool>();

    config.gray_enable = yml["gray_enable"].as<bool>();
    config.polarity_holes = yml["polarity_holes"].as<bool>();

    switch (yml["phase"].as<int>()) {
    case 1: config.phase = PHASE_1; break;
    case 2: config.phase = PHASE_2; break;
    case 4: config.phase = PHASE_4; break;
    case 8: config.phase = PHASE_8; break;
    case 16: config.phase = PHASE_16; break;
    default:
        std::cerr << "Invalid phase." << std::endl;
        std::abort();
    }

    switch (yml["freq"].as<int>()) {
    case 40: config.freq = FREQ_40; break;
    case 80: config.freq = FREQ_80; break;
    case 160: config.freq = FREQ_160; break;
    default:
        std::cerr << "Invalid frequency." << std::endl;
        std::abort();
    }

    auto yml_dac = yml["dacs"].as<YAML::Node>();
    config.dacs.named.Ibias_Preamp_ON       = yml_dac["Ibias_Preamp_ON"].as<uint16_t>();
    config.dacs.named.Ibias_Preamp_OFF      = yml_dac["Ibias_Preamp_OFF"].as<uint16_t>();
    config.dacs.named.VPReamp_NCAS          = yml_dac["VPReamp_NCAS"].as<uint16_t>();
    config.dacs.named.Ibias_Ikrum           = yml_dac["Ibias_Ikrum"].as<uint16_t>();
    config.dacs.named.Vfbk                  = yml_dac["Vfbk"].as<uint16_t>();
    config.dacs.named.Vthreshold_fine       = yml_dac["Vthreshold_fine"].as<uint16_t>();
    config.dacs.named.Vthreshold_coarse     = yml_dac["Vthreshold_coarse"].as<uint16_t>();
    config.dacs.named.Ibias_DiscS1_ON       = yml_dac["Ibias_DiscS1_ON"].as<uint16_t>();
    config.dacs.named.Ibias_DiscS1_OFF      = yml_dac["Ibias_DiscS1_OFF"].as<uint16_t>();
    config.dacs.named.Ibias_DiscS2_ON       = yml_dac["Ibias_DiscS2_ON"].as<uint16_t>();
    config.dacs.named.Ibias_DiscS2_OFF      = yml_dac["Ibias_DiscS2_OFF"].as<uint16_t>();
    config.dacs.named.Ibias_PixelDAC        = yml_dac["Ibias_PixelDAC"].as<uint16_t>();
    config.dacs.named.Ibias_TPbufferIn      = yml_dac["Ibias_TPbufferIn"].as<uint16_t>();
    config.dacs.named.Ibias_TPbufferOut     = yml_dac["Ibias_TPbufferOut"].as<uint16_t>();
    config.dacs.named.VTP_coarse            = yml_dac["VTP_coarse"].as<uint16_t>();
    config.dacs.named.VTP_fine              = yml_dac["VTP_fine"].as<uint16_t>();
    config.dacs.named.Ibias_CP_PLL          = yml_dac["Ibias_CP_PLL"].as<uint16_t>();
    config.dacs.named.PLL_Vcntrl            = yml_dac["PLL_Vcntrl"].as<uint16_t>();

    {
        int res;
        auto bmc_path = yml["pixel_config"].as<std::string>();
        FILE* f = fopen(bmc_path.c_str(), "r");
        char* buffer = new char[65536];
        fread(buffer, 1, 65536, f);

        katherine_bmc_init(&config.pixel_config);
        res = katherine_bmc_load(&config.pixel_config, buffer);

        if (res) {
            std::cerr << "Could not load BMC pixel configuration." << std::endl;
            std::abort();
        }

        delete[] buffer;
        fclose(f);
    }
}

void
run_acquisition(katherine_device_t *device, const YAML::Node& yml)
{
    using namespace std::chrono;

    int res;
    katherine_config_t config{};

    katherine_readout_type_t readout = READOUT_DATA_DRIVEN;
    configure_yaml(config, yml);

    katherine_acquisition_t acq{};
    res = katherine_acquisition_init(&acq, device, KATHERINE_MD_SIZE * yml["buffer_md_items"].as<std::size_t>(), sizeof(katherine_px_f_toa_tot_t) * yml["buffer_px_items"].as<std::size_t>());

    if (res) {
        std::lock_guard<std::mutex> lk{cerr_mutex};
        std::cerr << "Could not prepare acquisition. Is the configuration valid?" << std::endl;
        std::abort();
    }

    acq.handlers.frame_started = frame_started;
    acq.handlers.frame_ended = frame_ended;
    acq.handlers.pixels_received = pixels_received;

    res = katherine_acquisition_begin(&acq, &config, readout);

    if (res) {
        std::lock_guard<std::mutex> lk{cerr_mutex};
        std::cerr << "Could not start acquisition. Is the configuration valid?" << std::endl;
        std::abort();
    }

    {
        std::lock_guard<std::mutex> lk{cerr_mutex};
        std::cerr << "Acquisition started." << std::endl;
        std::cerr << std::endl;
    }

    auto tic = steady_clock::now();

    res = katherine_acquisition_read(&acq);

    auto toc = steady_clock::now();
    double duration = duration_cast<milliseconds>(toc - tic).count() / 1000.;

    {
        std::lock_guard<std::mutex> lk{cerr_mutex};
        std::cerr << std::endl;
        std::cerr << "Acquisition completed:" << std::endl
                  << " - state: " << katherine_str_acquisition_status(acq.state) << std::endl
                  << " - received " << acq.completed_frames << " complete frames" << std::endl
                  << " - dropped " << acq.dropped_measurement_data << " measurement data items" << std::endl
                  << " - total hits: " << n_hits << std::endl
                  << " - total duration: " << duration << " s" << std::endl
                  << " - throughput: " << (n_hits / duration) << " hits/s" << std::endl;
    }
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <config file>" << std::endl;
        std::abort();
    }

    YAML::Node yml = YAML::LoadFile(argv[1]);
    auto remote_addr = yml["ip"].as<std::string>();

    katherine_device_t device{};
    if (katherine_device_init(&device, remote_addr.c_str())) {
        std::cerr << "Invalid device address." << std::endl;
        std::abort();
    }

    print_chip_id(&device);
    run_acquisition(&device, yml);

    katherine_device_fini(&device);
}
*/
int main() { /* FIXME */ }