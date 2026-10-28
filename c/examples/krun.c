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

#include <inttypes.h> // PRIu64
#include <stdlib.h>   // exit
#include <stdio.h>    // printf
#include <time.h>     // time, difftime

#include <katherine/katherine.h>

#include "kargs.h"

static const char *remote_address = "udp://192.168.1.145";
typedef katherine_tpx3_px_f_toa_tot_t px_t;

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
paint_test_pixels(katherine_tpx3_px_config_t *px_config)
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

                    const katherine_tpx3_coord_t coord = {
                        .x = (uint8_t) x,
                        .y = (uint8_t) y,
                    };
                    katherine_tpx3_px_config_set_test_bit(px_config, coord, true);
                    ++n_painted;
                }
            }
        }
    }

    printf("Test pulses enabled for %d pixels.\n", n_painted);
}

void
configure(katherine_tpx3_config_t *config)
{
    // For now, these constants are hard-coded.
    // This configuration will produce meaningful results only for: I8-W0036

    config->common.chip_type  = KATHERINE_CHIP_TPX3;
    config->common.chip_count = 1;

    // We will configure settings from the most general to the most specific.
    // First we start with settings common to all chips.
    config->common.frame_duration_seconds = 10.0;
    config->common.frame_count            = 1;
    config->common.bias[0]                = 150; // V

    // The following structures can be used to delay the beginning or the end
    // of an acquisition based on external signals, connected to GPIO ports.
    // Here we just disable all, so that data collection starts immediately
    // and stops after a set time elapses.
    config->common.start_trigger.use_trigger   = false;
    config->common.start_trigger.channel       = KATHERINE_TRIGGER_SOURCE_PULSE_INTERNAL;
    config->common.start_trigger.edge          = KATHERINE_TRIGGER_EDGE_RISING;
    config->common.start_trigger.delayed       = false;
    config->common.start_trigger.delay_seconds = 0.;
    config->common.stop_trigger.use_trigger    = false;
    config->common.stop_trigger.channel        = KATHERINE_TRIGGER_SOURCE_PULSE_INTERNAL;
    config->common.stop_trigger.edge           = KATHERINE_TRIGGER_EDGE_RISING;
    config->common.stop_trigger.delayed        = false;
    config->common.stop_trigger.delay_seconds  = 0.;

    // Next we proceed to settings that only apply to Timepix3 chips.
    config->readout_mode    = KATHERINE_TPX3_READOUT_DATA_DRIVEN;
    config->px_mode         = KATHERINE_TPX3_PX_TOA_TOT;
    config->fast_vco_enable = true;

    config->clock_frequency     = KATHERINE_TPX3_FREQ_40_MHZ;
    config->clock_phase         = KATHERINE_TPX3_PHASE_1;
    config->correct_clock_phase = false; // only makes sense when we request more than 1 phase

    // And now we configure the first Timepix3 chip specifically. If the
    // readout carries more chips than one, the following block has to be
    // repeated for each chip.
    katherine_tpx3_chip_config_t *chip = &config->chips[0];

    chip->gray_disable = false;
    chip->polarity     = KATHERINE_POLARITY_HOLES;

    // The DAC setpoints determine how signals are digitized in all pixels.
    // For instance, Vthreshold_coarse and _fine sets the threshold voltage.
    chip->dacs.named.Ibias_Preamp_ON   = 128;
    chip->dacs.named.Ibias_Preamp_OFF  = 8;
    chip->dacs.named.Vpreamp_NCAS      = 128;
    chip->dacs.named.Ibias_Ikrum       = 15;
    chip->dacs.named.Vfbk              = 164;
    chip->dacs.named.Vthreshold_fine   = 367;
    chip->dacs.named.Vthreshold_coarse = 7;
    chip->dacs.named.Ibias_DiscS1_ON   = 100;
    chip->dacs.named.Ibias_DiscS1_OFF  = 8;
    chip->dacs.named.Ibias_DiscS2_ON   = 128;
    chip->dacs.named.Ibias_DiscS2_OFF  = 8;
    chip->dacs.named.Ibias_PixelDAC    = 128;
    chip->dacs.named.Ibias_TPbufferIn  = 128;
    chip->dacs.named.Ibias_TPbufferOut = 128;
    chip->dacs.named.VTP_coarse        = 128;
    chip->dacs.named.VTP_fine          = 256;
    chip->dacs.named.Ibias_CP_PLL      = 128;
    chip->dacs.named.PLL_Vcntrl        = 128;

    // Now we load values that will be set into registers of individual pixels.
    // These are typically kept in a binary file, which needs to be supplied
    // for each chip individually after chips are equalized. Alternatively,
    // libkatherine also provides a number of functions to create these values
    // from scratch in memory, or manipulate individual pixels (e.g. for tests).
    katherine_error_t res = katherine_tpx3_px_config_load_bmc_file(&chip->pixels, "chipconfig.bmc");
    if (res != KATHERINE_E_OK) {
        printf("Cannot load pixel configuration. Does the file exist?\n");
        printf("Reason: %s\n", katherine_strerror(res));
        exit(1);
    }

    // Test pulses are disabled unless requested above. Even though these are
    // zeroed by calloc earlier, it is a good practice not to rely on that.
    config->test_pulses = (katherine_tpx3_test_pulse_config_t) {0};
    if (test_pulse) {
        // If enabled, they are generated in a common way for all chips. Below
        // we will see, however, that each chip can define its own test-pulse
        // voltage using the "VTP" DAC setting.
        config->test_pulses.enabled      = true;
        config->test_pulses.digital_only = false;
        config->test_pulses.external     = false;
        config->test_pulses.count        = 100;
        config->test_pulses.period       = 6401; // clock cycles, ~160 us @ 40 MHz
        config->test_pulses.phase        = 0;

        // Pulse the analog front-end with amplitude given by the difference
        // of the two DACs: |128 * 5 mV - 352 * 2.5 mV| = 240 mV.
        chip->dacs.named.VTP_coarse = 128;
        chip->dacs.named.VTP_fine   = 352;

        // And of course, each pixel in the chip gets a "test" bit which determines
        // whether test pulses are injected into it or not. We can demonstrate this
        // by flipping this bit in a specific subset of pixels to paint a recognizable
        // picture.
        paint_test_pixels(&chip->pixels);
    }
}

static uint64_t n_hits;

// The shift taking a coarse tick to the fine ticks inside it. Resolved from
// the clock frequency this program configured, by the same function the
// library resolves it with -- a handler is given the acquisition, and what
// the decoder keeps inside one is the decoder's.
static uint8_t g_toa_shift;

void
frame_started(void *user_ctx, uint32_t frame_idx)
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
frame_ended(void *user_ctx, uint32_t frame_idx, bool completed, const katherine_frame_info_t *info)
{
    (void) user_ctx;

    printf("\n");
    printf("Ended frame %d.\n", frame_idx);
    printf(" - tpx3->katherine lost %" PRIu64 " pixels\n", info->lost_pixels);
    printf(" - katherine->pc sent %" PRIu64 " pixels\n", info->sent_pixels);
    printf(" - katherine->pc received %" PRIu64 " pixels\n", info->received_pixels);
    printf(" - state: %s\n", (completed ? "completed" : "not completed"));
    printf(" - frame %u started at: %" PRIu64 "\n", frame_idx, info->start_time[0].d);
    printf(" - frame %u ended at: %" PRIu64 "\n", frame_idx, info->end_time[0].d);

    // One tail per chip, and the chips' timers need not agree: on the
    // two-chip readout they sit a constant 55.4 ms apart. This example
    // configures a single chip, so there is only ever [0] to read; a
    // multi-chip program walks info->reporting_chips.
    if (info->reporting_chips > 1) {
        printf(" - note: %u chips reported, only the first is shown\n", info->reporting_chips);
    }
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
    const uint8_t shift = g_toa_shift;

    const px_t *dpx = (const px_t *) px;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t phase = katherine_tpx3_timestamp_phase_offset(acq, dpx[i].coord);

        uint64_t toa = 0;
        uint8_t ftoa = 0;
        katherine_tpx3_timestamp_to_toa_ftoa(shift, phase, dpx[i].timestamp, &toa, &ftoa);

        printf("%d\t%d\t%" PRIu64 "\t%d\t%d\n", dpx[i].coord.x, dpx[i].coord.y, toa, ftoa, dpx[i].tot);
    }
}

void
print_chip_id(katherine_device_t *device)
{
    // Expecting exactly 1 chip.
    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    int res = katherine_get_chip_id(device, 0, chip_id);
    if (res != 0) {
        printf("Cannot get chip ID. Is Timepix3 connected to the readout?\n");
        printf("Reason: %s\n", katherine_strerror(res));
        exit(2);
    }

    printf("Chip ID: %s\n", chip_id);
}

void
run_acquisition(katherine_device_t *dev, const katherine_tpx3_config_t *c)
{
    int res;
    katherine_acquisition_t acq;

    // The handlers are given the acquisition itself: they need the divider it
    // resolved in order to interpret the timestamps it produces.
    res = katherine_acquisition_init(
        &acq, dev, &acq, KATHERINE_TPX3_MD_SIZE * 34952533, sizeof(px_t) * 65536, 500, 10000);
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

    g_toa_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(c->clock_frequency);

    res = katherine_acquisition_begin(&acq, c, true);
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
    printf(" - state: %s\n", katherine_str_acquisition_state(acq.state));
    printf(" - received %d complete frames\n", acq.completed_frames);
    printf(" - dropped %zu measurement data\n", acq.dropped_measurement_data);
    printf(" - total hits: %" PRIu64 "\n", n_hits);
    printf(" - total duration: %f s\n", duration);
    printf(" - throughput: %f hits/s\n", (n_hits / duration));

    katherine_acquisition_fini(&acq);
}

// To stdout for --help; this example has no usage error to report, every
// argument being an address.
static void
usage(FILE *out, const char *argv0)
{
    fprintf(out, "Usage: %s [<address>]\n", argv0);
    fprintf(out, "  e.g. %s udp://192.168.1.42\n", argv0);
    fprintf(out, "  Runs one acquisition against the readout and reports its throughput.\n");
    fprintf(out, "  Needs chipconfig.bmc in the working directory.\n");
    fprintf(out, "  With no argument, uses %s.\n", remote_address);
}

int
main(int argc, char *argv[])
{
    if (kargs_wants_help(argc, argv)) {
        usage(stdout, argv[0]);
        return 0;
    }

    if (argc > 1) {
        remote_address = argv[1];
    }
    printf("Using remote address: %s\n", remote_address);

    int res;
    katherine_device_t device;

    res = katherine_device_init(&device, remote_address, 0);
    if (res != 0) {
        printf("Cannot initialize device. Is the address correct?\n");
        printf("Reason: %s\n", katherine_strerror(res));
        exit(6);
    }

    // The configuration structure includes a flexible array at the end, which
    // holds per-chip settings, and must correspond in size to the number of
    // detected chips. Here we assume exactly 1 chip for the sake of simplicity,
    // however in other applications users may wish to retrieve the number of
    // detected chips at runtime (see `device.info.chip_count`) or use an
    // adequate upper bound.
    const uint8_t expected_chip_count = 1;
    if (device.info.chip_count != expected_chip_count) {
        printf("Refusing to proceed due to chip count mismatch: expected %u, but got %u instead.\n",
            (unsigned) expected_chip_count, (unsigned) device.info.chip_count);
        exit(6);
    }

    // Using calloc rather than malloc: 0 is a safe value for every field here.
    katherine_tpx3_config_t *config = calloc(1, KATHERINE_TPX3_CONFIG_SIZE(expected_chip_count));
    if (config == NULL) {
        printf("Cannot allocate a configuration for %u chips.\n", (unsigned) expected_chip_count);
        exit(6);
    }
    configure(config);

    print_chip_id(&device);
    run_acquisition(&device, config);

    free(config);
    katherine_device_fini(&device);
    return 0;
}
