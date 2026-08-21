/**
 * @file
 * @brief Example: configure and perform data-driven acquisition.
 * @author Petr Mánek
 * @date 13.07.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h> // exit
#include <stdio.h>  // printf
#include <time.h>   // time, difftime
#include <string.h> // strerror

#include <katherine/katherine.h>

static const char *remote_addr = "192.168.1.145";
typedef katherine_px_f_toa_tot_t px_t;

// Set the following flag to inject test pulses into a small subset of
// pixels during the acquisition.
static const bool test_pulse = false;

void
paint_test_pixels(katherine_px_config_t *px_config)
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
    static const int n_rows = sizeof(shape) / sizeof(shape[0]);
    static const int scale  = 5;         // each shape cell becomes a scale x scale block of pixels
    static const int x0 = 120, y0 = 120; // bottom-left corner of the shape
    int n_painted = 0;

    for (int row = 0; row < n_rows; ++row) {
        for (int col = 0; shape[row][col]; ++col) {
            if (shape[row][col] != 'X') continue;

            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    // Rows of the shape are listed top-down while the Y axis
                    // grows upwards, hence the flipped row index.
                    const int x = x0 + scale * col + dx;
                    const int y = y0 + scale * (n_rows - 1 - row) + dy;

                    if (x < 0 || x > 255 || y < 0 || y > 255) {
                        printf("Test pulse pixel at X = %d,\t Y = %d is out of bounds, skipping.\n", x, y);
                        continue;
                    }

                    const katherine_coord_t coord = {
                        .x = (uint8_t) x,
                        .y = (uint8_t) y,
                    };
                    katherine_px_config_set_test_bit(px_config, coord, true);
                    ++n_painted;
                }
            }
        }
    }

    printf("Test pulses enabled for %d pixels.\n", n_painted);
}

void
configure(katherine_config_t *config)
{
    // For now, these constants are hard-coded.
    // This configuration will produce meaningful results only for: K7-W0005
    config->bias_id   = 0;
    config->acq_time  = 10e9; // ns
    config->no_frames = 1;
    config->bias      = 230; // V

    config->delayed_start = false;

    config->start_trigger.enabled          = false;
    config->start_trigger.channel          = 0;
    config->start_trigger.use_falling_edge = false;
    config->stop_trigger.enabled           = false;
    config->stop_trigger.channel           = 0;
    config->stop_trigger.use_falling_edge  = false;

    config->gray_disable   = false;
    config->polarity_holes = false;

    config->phase = PHASE_1;
    config->freq  = FREQ_40;

    config->dacs.named.Ibias_Preamp_ON   = 128;
    config->dacs.named.Ibias_Preamp_OFF  = 8;
    config->dacs.named.VPReamp_NCAS      = 128;
    config->dacs.named.Ibias_Ikrum       = 15;
    config->dacs.named.Vfbk              = 164;
    config->dacs.named.Vthreshold_fine   = 476;
    config->dacs.named.Vthreshold_coarse = 8;
    config->dacs.named.Ibias_DiscS1_ON   = 100;
    config->dacs.named.Ibias_DiscS1_OFF  = 8;
    config->dacs.named.Ibias_DiscS2_ON   = 128;
    config->dacs.named.Ibias_DiscS2_OFF  = 8;
    config->dacs.named.Ibias_PixelDAC    = 128;
    config->dacs.named.Ibias_TPbufferIn  = 128;
    config->dacs.named.Ibias_TPbufferOut = 128;
    config->dacs.named.VTP_coarse        = 128;
    config->dacs.named.VTP_fine          = 256;
    config->dacs.named.Ibias_CP_PLL      = 128;
    config->dacs.named.PLL_Vcntrl        = 128;

    int res = katherine_px_config_load_bmc_file(&config->pixel_config, "chipconfig.bmc");
    if (res != 0) {
        printf("Cannot load pixel configuration. Does the file exist?\n");
        printf("Reason: %s\n", strerror(res));
        exit(1);
    }

    // Test pulses are disabled unless requested above.
    config->test_pulse_config = (katherine_test_pulse_config_t) {0};

    if (test_pulse) {
        // Pulse the analog front-end with amplitude given by the difference
        // of the two DACs: |128 * 5 mV - 352 * 2.5 mV| = 240 mV.
        config->dacs.named.VTP_coarse = 128;
        config->dacs.named.VTP_fine   = 352;

        config->test_pulse_config.enabled      = true;
        config->test_pulse_config.digital_only = false;
        config->test_pulse_config.external     = false;
        config->test_pulse_config.count        = 100;
        config->test_pulse_config.period       = 6401; // clock cycles, ~160 us @ 40 MHz
        config->test_pulse_config.phase        = 0;

        paint_test_pixels(&config->pixel_config);
    }
}

static uint64_t n_hits;

void
frame_started(void *user_ctx, int frame_idx)
{
    n_hits = 0;

    printf("Started frame %d.\n", frame_idx);
    printf("X\tY\tToA\tfToA\tToT\n");
}

void
frame_ended(void *user_ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
{
    const double recv_perc = 100. * info->received_pixels / info->sent_pixels;

    printf("\n");
    printf("Ended frame %d.\n", frame_idx);
    printf(" - tpx3->katherine lost %lu pixels\n", info->lost_pixels);
    printf(" - katherine->pc sent %lu pixels\n", info->sent_pixels);
    printf(" - katherine->pc received %lu pixels\n", info->received_pixels);
    printf(" - state: %s\n", (completed ? "completed" : "not completed"));
    printf(" - start time: %lu\n", info->start_time.d);
    printf(" - end time: %lu\n", info->end_time.d);
}

void
pixels_received(void *user_ctx, const void *px, size_t count)
{
    n_hits += count;

    const px_t *dpx = (const px_t *) px;
    for (size_t i = 0; i < count; ++i) {
        printf("%d\t%d\t%lu\t%d\t%d\n", dpx[i].coord.x, dpx[i].coord.y, dpx[i].toa, dpx[i].ftoa, dpx[i].tot);
    }
}

void
print_chip_id(katherine_device_t *device)
{
    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    int res = katherine_get_chip_id(device, chip_id);
    if (res != 0) {
        printf("Cannot get chip ID. Is Timepix3 connected to the readout?\n");
        printf("Reason: %s\n", strerror(res));
        exit(2);
    }

    printf("Chip ID: %s\n", chip_id);
}

void
run_acquisition(katherine_device_t *dev, const katherine_config_t *c)
{
    int res;
    katherine_acquisition_t acq;

    res = katherine_acquisition_init(&acq, dev, NULL, KATHERINE_MD_SIZE * 34952533, sizeof(px_t) * 65536, 500, 10000);
    if (res != 0) {
        printf("Cannot initialize acquisition. Is the configuration valid?\n");
        printf("Reason: %s\n", strerror(res));
        exit(3);
    }

    acq.handlers.frame_started   = frame_started;
    acq.handlers.frame_ended     = frame_ended;
    acq.handlers.pixels_received = pixels_received;

    res = katherine_acquisition_begin(&acq, c, READOUT_DATA_DRIVEN, ACQUISITION_MODE_TOA_TOT, true, true);
    if (res != 0) {
        printf("Cannot begin acquisition.\n");
        printf("Reason: %s\n", strerror(res));
        exit(4);
    }

    printf("Acquisition started.\n");

    time_t tic = time(NULL);
    res        = katherine_acquisition_read(&acq);
    if (res != 0) {
        printf("Cannot read acquisition data.\n");
        printf("Reason: %s\n", strerror(res));
        exit(5);
    }
    time_t toc = time(NULL);

    double duration = difftime(toc, tic);
    ;
    printf("\n");
    printf("Acquisition completed:\n");
    printf(" - state: %s\n", katherine_str_acquisition_status(acq.state));
    printf(" - received %d complete frames\n", acq.completed_frames);
    printf(" - dropped %zu measurement data\n", acq.dropped_measurement_data);
    printf(" - total hits: %lu\n", n_hits);
    printf(" - total duration: %f s\n", duration);
    printf(" - throughput: %f hits/s\n", (n_hits / duration));

    katherine_acquisition_fini(&acq);
}

int
main(int argc, char *argv[])
{
    if (argc > 1) {
        remote_addr = argv[1];
    }
    printf("Using remote address: %s\n", remote_addr);

    katherine_config_t c;
    configure(&c);

    int res;
    katherine_device_t device;

    res = katherine_device_init(&device, remote_addr);
    if (res != 0) {
        printf("Cannot initialize device. Is the address correct?\n");
        printf("Reason: %s\n", strerror(res));
        exit(6);
    }

    print_chip_id(&device);
    run_acquisition(&device, &c);

    katherine_device_fini(&device);
    return 0;
}
