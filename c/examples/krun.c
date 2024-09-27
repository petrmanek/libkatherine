//
// Created by Petr Mánek on 13.07.18.
//

#include <stdlib.h>     // exit
#include <stdio.h>      // printf
#include <time.h>       // time, difftime
#include <string.h>     // strerror

#include <katherine/katherine.h>

static const char *remote_addr = "192.168.1.145";
typedef katherine_px_f_toa_tot_t px_t;

void
configure(katherine_config_t *config)
{
    // For now, these constants are hard-coded.
    // This configuration will produce meaningful results only for: K7-W0005
    config->bias_id                 = 0;
    config->acq_time                = 10e9; // ns
    config->no_frames               = 1;
    config->bias                    = 230; // V

    config->delayed_start           = false;

    config->start_trigger.enabled           = false;
    config->start_trigger.channel           = 0;
    config->start_trigger.use_falling_edge  = false;
    config->stop_trigger.enabled            = false;
    config->stop_trigger.channel            = 0;
    config->stop_trigger.use_falling_edge   = false;

    config->gray_disable            = false;
    config->polarity_holes          = false;

    config->phase                   = PHASE_1;
    config->freq                    = FREQ_40;

    config->dacs.named.Ibias_Preamp_ON       = 128;
    config->dacs.named.Ibias_Preamp_OFF      = 8;
    config->dacs.named.VPReamp_NCAS          = 128;
    config->dacs.named.Ibias_Ikrum           = 15;
    config->dacs.named.Vfbk                  = 164;
    config->dacs.named.Vthreshold_fine       = 476;
    config->dacs.named.Vthreshold_coarse     = 8;
    config->dacs.named.Ibias_DiscS1_ON       = 100;
    config->dacs.named.Ibias_DiscS1_OFF      = 8;
    config->dacs.named.Ibias_DiscS2_ON       = 128;
    config->dacs.named.Ibias_DiscS2_OFF      = 8;
    config->dacs.named.Ibias_PixelDAC        = 128;
    config->dacs.named.Ibias_TPbufferIn      = 128;
    config->dacs.named.Ibias_TPbufferOut     = 128;
    config->dacs.named.VTP_coarse            = 128;
    config->dacs.named.VTP_fine              = 256;
    config->dacs.named.Ibias_CP_PLL          = 128;
    config->dacs.named.PLL_Vcntrl            = 128;

    int res = katherine_px_config_load_bmc_file(&config->pixel_config, "chipconfig.bmc");
    if (res != 0) {
        printf("Cannot load pixel configuration. Does the file exist?\n");
        printf("Reason: %s\n", strerror(res));
        exit(1);
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

    acq.handlers.frame_started = frame_started;
    acq.handlers.frame_ended = frame_ended;
    acq.handlers.pixels_received = pixels_received;

    res = katherine_acquisition_begin(&acq, c, READOUT_DATA_DRIVEN, ACQUISITION_MODE_TOA_TOT, true, true);
    if (res != 0) {
        printf("Cannot begin acquisition.\n");
        printf("Reason: %s\n", strerror(res));
        exit(4);
    }

    printf("Acquisition started.\n");

    time_t tic = time(NULL);
    res = katherine_acquisition_read(&acq);
    if (res != 0) {
        printf("Cannot read acquisition data.\n");
        printf("Reason: %s\n", strerror(res));
        exit(5);
    }
    time_t toc = time(NULL);

    double duration = difftime(toc, tic);;
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
