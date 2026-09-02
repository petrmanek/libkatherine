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

#include <inttypes.h> // PRIu64
#include <stdlib.h>   // exit
#include <stdio.h>    // printf
#include <time.h>     // time, difftime

#include <katherine/katherine.h>

static const char *remote_addr = "192.168.1.145";
typedef katherine_px_f_toa_tot_t px_t;

// Set the following flag to inject test pulses into a small subset of
// pixels during the acquisition.
static const bool test_pulse = false;

// How the hit lines below report time. Each costs a little more than the last
// and says a little more; see the three handlers for what that buys.
typedef enum krun_toa_format {
    KRUN_TOA_TIMESTAMP, /* the decoded value as stored */
    KRUN_TOA_SECONDS,   /* whole seconds and nanoseconds within them */
    KRUN_TOA_COUNTERS,  /* the sensor's own ToA and fToA */
} krun_toa_format_t;

static const krun_toa_format_t print_toa = KRUN_TOA_SECONDS;

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

    config->phase         = PHASE_1;
    config->correct_phase = false; // only makes sense to correct phase offset when we request more than 1 phase

    config->freq = FREQ_40;

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
        printf("Reason: %s\n", katherine_strerror(res));
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
    (void) user_ctx;

    n_hits = 0;

    printf("Started frame %d.\n", frame_idx);

    // Column labels have to follow the format actually printed, or a reader
    // silently misreads the wrong quantity. Not performance-critical: this
    // runs once per frame, not once per hit.
    if (print_toa == KRUN_TOA_TIMESTAMP) {
        printf("X\tY\tTimestamp\tToT\n");
    } else if (print_toa == KRUN_TOA_SECONDS) {
        printf("X\tY\tSeconds\tNanoseconds\tToT\n");
    } else {
        printf("X\tY\tToA\tfToA\tToT\n");
    }
}

void
frame_ended(void *user_ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
{
    (void) user_ctx;

    printf("\n");
    printf("Ended frame %d.\n", frame_idx);
    printf(" - tpx3->katherine lost %" PRIu64 " pixels\n", info->lost_pixels);
    printf(" - katherine->pc sent %" PRIu64 " pixels\n", info->sent_pixels);
    printf(" - katherine->pc received %" PRIu64 " pixels\n", info->received_pixels);
    printf(" - state: %s\n", (completed ? "completed" : "not completed"));
    printf(" - start time: %" PRIu64 "\n", info->start_time.d);
    printf(" - end time: %" PRIu64 "\n", info->end_time.d);
}

// The timestamp as the decoder produced it: fine-oscillator ticks, already
// combined from the sensor's two counters. Nothing to call and nothing to get
// wrong -- and because it is one monotonic integer, hits compare and subtract
// directly, which is what ordering and clustering need. The unit is arbitrary
// but uniform, so differences are meaningful even though absolute values are
// not a physical time.
void
pixels_received_timestamp(void *user_ctx, const void *px, size_t count)
{
    (void) user_ctx;

    n_hits += count;

    const px_t *dpx = (const px_t *) px;
    for (size_t i = 0; i < count; ++i) {
        printf("%d\t%d\t%" PRIu64 "\t%d\n", dpx[i].coord.x, dpx[i].coord.y, dpx[i].timestamp, dpx[i].tot);
    }
}

// Physical time, which is what most consumers actually want. One call per hit
// buys it, and the split into whole seconds and nanoseconds within them is
// what keeps the result exact: a single double cannot hold the range a long
// acquisition covers at this resolution. Summing the two parts back together
// throws that away again.
void
pixels_received_seconds(void *user_ctx, const void *px, size_t count)
{
    (void) user_ctx;

    n_hits += count;

    const px_t *dpx = (const px_t *) px;
    for (size_t i = 0; i < count; ++i) {
        uint64_t sec = 0;
        double nsec  = 0.0;
        katherine_tpx3_timestamp_to_seconds(dpx[i].timestamp, &sec, &nsec);

        printf("%d\t%d\t%" PRIu64 "\t%.4f\t%d\n", dpx[i].coord.x, dpx[i].coord.y, sec, nsec, dpx[i].tot);
    }
}

// The sensor's own counters, for comparing against a raw capture or against
// what libkatherine reported before timestamps existed. The decoder combined
// them, so this undoes that -- including the offset applied to this pixel's
// double column, which is read rather than assumed zero so the code stays
// right if phase correction is switched on. Needs the acquisition, both for
// the divider it resolved and for that offset.
void
pixels_received_counters(void *user_ctx, const void *px, size_t count)
{
    const katherine_acquisition_t *acq = (const katherine_acquisition_t *) user_ctx;

    n_hits += count;

    // Fixed for the acquisition, so it is read once rather than per hit.
    const uint8_t shift = acq->toa_coarse_tick_to_fine_shift;

    const px_t *dpx = (const px_t *) px;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t phase = katherine_acquisition_timestamp_phase_offset(acq, dpx[i].coord);

        uint64_t toa = 0;
        uint8_t ftoa = 0;
        katherine_tpx3_timestamp_to_toa_ftoa(shift, phase, dpx[i].timestamp, &toa, &ftoa);

        printf("%d\t%d\t%" PRIu64 "\t%d\t%d\n", dpx[i].coord.x, dpx[i].coord.y, toa, ftoa, dpx[i].tot);
    }
}

void
print_chip_id(katherine_device_t *device)
{
    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    int res = katherine_get_chip_id(device, chip_id);
    if (res != 0) {
        printf("Cannot get chip ID. Is Timepix3 connected to the readout?\n");
        printf("Reason: %s\n", katherine_strerror(res));
        exit(2);
    }

    printf("Chip ID: %s\n", chip_id);
}

void
run_acquisition(katherine_device_t *dev, const katherine_config_t *c)
{
    int res;
    katherine_acquisition_t acq;

    // The handlers are given the acquisition itself: they need the divider it
    // resolved in order to interpret the timestamps it produces.
    res = katherine_acquisition_init(
        &acq, dev, &acq, KATHERINE_MD_SIZE * 34952533, sizeof(px_t) * 65536, 500, 10000);
    if (res != 0) {
        printf("Cannot initialize acquisition. Is the configuration valid?\n");
        printf("Reason: %s\n", katherine_strerror(res));
        exit(3);
    }

    acq.handlers.frame_started = frame_started;
    acq.handlers.frame_ended   = frame_ended;
    if (print_toa == KRUN_TOA_TIMESTAMP) {
        acq.handlers.pixels_received = pixels_received_timestamp;
    } else if (print_toa == KRUN_TOA_SECONDS) {
        acq.handlers.pixels_received = pixels_received_seconds;
    } else {
        acq.handlers.pixels_received = pixels_received_counters;
    }

    res = katherine_acquisition_begin(&acq, c, READOUT_DATA_DRIVEN, ACQUISITION_MODE_TOA_TOT, true, true);
    if (res != 0) {
        printf("Cannot begin acquisition.\n");
        printf("Reason: %s\n", katherine_strerror(res));
        exit(4);
    }

    printf("Acquisition started.\n");

    time_t tic = time(NULL);
    res        = katherine_acquisition_read(&acq);
    if (res != 0) {
        printf("Cannot read acquisition data.\n");
        printf("Reason: %s\n", katherine_strerror(res));
        exit(5);
    }
    time_t toc = time(NULL);

    double duration = difftime(toc, tic);

    printf("\n");
    printf("Acquisition completed:\n");
    printf(" - state: %s\n", katherine_str_acquisition_status(acq.state));
    printf(" - received %d complete frames\n", acq.completed_frames);
    printf(" - dropped %zu measurement data\n", acq.dropped_measurement_data);
    printf(" - total hits: %" PRIu64 "\n", n_hits);
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
        printf("Reason: %s\n", katherine_strerror(res));
        exit(6);
    }

    print_chip_id(&device);
    run_acquisition(&device, &c);

    katherine_device_fini(&device);
    return 0;
}
