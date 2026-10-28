/**
 * \file
 * \brief Frame boundaries a readout did not finish.
 *
 * katherine_acquisition_t's handlers report a frame opening and a frame
 * closing, and a caller that opens a file per frame pairs them. The read
 * loop closes an unfinished frame when the read ends -- but it used not to
 * when the *next* frame started, so a lost frame-finished datum left two
 * frames' hits arriving as one, with nothing to say so.
 *
 * A measurement datum's header nibble is the top four bits of its last
 * byte: 0x7 opens a frame, 0xC finishes one, and 0x0 is a pixel on the
 * second generation's chip 0. That is the whole stream grammar these cases
 * need, so they are written as byte patterns rather than captured.
 *
 * \author Petr Mánek
 * \date 25.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include <katherine/katherine.h>

#include "kemu_transport.h"
#include "ktest.h"

#define MD_NEW_FRAME      0x7
#define MD_FRAME_FINISHED 0xC
#define MD_PIXEL_CHIP_0   0x0

#define CONFIG_SIZE       (sizeof(katherine_tpx3_config_t) + sizeof(katherine_tpx3_chip_config_t))
#define MD_BUFFER_SIZE    (KATHERINE_TPX3_MD_SIZE * 4096)
#define PIXEL_BUFFER      4096
#define REPORT_TIMEOUT_MS 50
#define FAIL_TIMEOUT_MS   150

static katherine_device_t g_device;
static kemu_transport_t g_link;
static uint8_t g_stream[8 * KATHERINE_TPX3_MD_SIZE];
static int g_started;
static int g_ended;
static int g_ended_complete;

static void
md(size_t index, uint8_t header)
{
    memset(g_stream + index * KATHERINE_TPX3_MD_SIZE, 0, KATHERINE_TPX3_MD_SIZE);
    g_stream[index * KATHERINE_TPX3_MD_SIZE + KATHERINE_TPX3_MD_SIZE - 1] = (uint8_t) (header << 4);
}

static void
on_started(void *ctx, int index)
{
    (void) ctx;
    (void) index;
    ++g_started;
}

static void
on_ended(void *ctx, int index, bool complete, const katherine_frame_info_t *info)
{
    (void) ctx;
    (void) index;
    (void) info;
    ++g_ended;
    if (complete) ++g_ended_complete;
}

static katherine_tpx3_config_t *
make_config(void)
{
    katherine_tpx3_config_t *config = (katherine_tpx3_config_t *) calloc(1, CONFIG_SIZE);
    if (config == NULL) return NULL;

    config->common.chip_type              = KATHERINE_CHIP_TPX3;
    config->common.chip_count             = 1;
    config->common.frame_duration_seconds = 0.0004;
    config->common.frame_count            = 1;

    config->px_mode         = KATHERINE_TPX3_PX_TOA_TOT;
    config->fast_vco_enable = true;
    config->clock_phase     = KATHERINE_TPX3_PHASE_1;
    config->clock_frequency = KATHERINE_TPX3_FREQ_40_MHZ;

    return config;
}

// Runs one read over the first n_md of g_stream and leaves the counts in
// g_started, g_ended and g_ended_complete.
static void
read_stream(size_t n_md, kemu_data_t *data)
{
    katherine_emu_profile_t profile;
    katherine_emu_profile_defaults(&profile);
    profile.hw_type     = 0x03;
    profile.hw_revision = 3;
    profile.fw_version  = 5;

    KT_REQUIRE(kemu_device(&g_device, &g_link, &profile) == KATHERINE_E_OK);

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(
                   &acq, &g_device, NULL, MD_BUFFER_SIZE, PIXEL_BUFFER, REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);

    memset(data, 0, sizeof(*data));
    data->md  = g_stream;
    data->len = n_md * KATHERINE_TPX3_MD_SIZE;
    kemu_data_attach(&acq, data);

    acq.handlers.frame_started = on_started;
    acq.handlers.frame_ended   = on_ended;

    katherine_tpx3_config_t *config = make_config();
    KT_REQUIRE(config != NULL);

    g_started        = 0;
    g_ended          = 0;
    g_ended_complete = 0;

    KT_REQUIRE(katherine_acquisition_begin(&acq, config, true) == KATHERINE_E_OK);
    (void) katherine_acquisition_read(&acq);

    kemu_data_detach(&acq);
    katherine_acquisition_fini(&acq);
    free(config);
    kemu_device_fini(&g_device, &g_link);
}

// ------------------------------------------------------------------

static void
test_a_finished_frame_is_reported_complete(void)
{
    md(0, MD_NEW_FRAME);
    md(1, MD_PIXEL_CHIP_0);
    md(2, MD_PIXEL_CHIP_0);
    md(3, MD_FRAME_FINISHED);

    kemu_data_t data;
    read_stream(4, &data);

    KT_CHECK_EQ(g_started, 1);
    KT_CHECK_EQ(g_ended, 1);
    KT_CHECK_EQ(g_ended_complete, 1);
    KT_CHECK_EQ(data.acquires, data.releases);
}

static void
test_a_frame_the_stream_abandons_is_still_reported(void)
{
    md(0, MD_NEW_FRAME);
    md(1, MD_PIXEL_CHIP_0);
    md(2, MD_PIXEL_CHIP_0);
    md(3, MD_PIXEL_CHIP_0);

    kemu_data_t data;
    read_stream(4, &data);

    KT_CHECK_EQ(g_started, 1);
    KT_CHECK_EQ(g_ended, 1);
    KT_CHECK_EQ(g_ended_complete, 0);
    KT_CHECK_EQ(data.acquires, data.releases);
}

// The one that used to go unreported: a frame-finished datum lost, so the
// next frame opens over an unfinished one.
static void
test_a_frame_superseded_by_the_next_is_reported(void)
{
    md(0, MD_NEW_FRAME);
    md(1, MD_PIXEL_CHIP_0);
    md(2, MD_NEW_FRAME);
    md(3, MD_PIXEL_CHIP_0);

    kemu_data_t data;
    read_stream(4, &data);

    KT_CHECK_EQ(g_started, 2);
    KT_CHECK_EQ(g_ended, 2);
    KT_CHECK_EQ(g_ended_complete, 0);
    KT_CHECK_EQ(data.acquires, data.releases);
}

int
main(void)
{
    KT_RUN(test_a_finished_frame_is_reported_complete);
    KT_RUN(test_a_frame_the_stream_abandons_is_still_reported);
    KT_RUN(test_a_frame_superseded_by_the_next_is_reported);
    return kt_summary();
}
