/* Katherine Control Library
 *
 * This file was created on 9.6.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <katherine/px_config.h>

/**
 * Load pixel configuration from a BMC file (in BurdaMan format).
 * @param px_config Target configuration matrix.
 * @param file_path BMC file path.
 * @return Error code.
 */
int
katherine_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path)
{
    int res = 0;

    const size_t expected_size = sizeof(katherine_bmc_t);
    FILE* file = fopen(file_path, "rb");
    if (file == NULL) {
        res = errno;
        goto err_fopen;
    }

    katherine_bmc_t *buffer = (katherine_bmc_t *) malloc(expected_size);
    if (buffer == NULL) {
        res = ENOMEM;
        goto err_buffer;
    }

    const size_t actual_size = fread(buffer, 1, expected_size, file);
    if (expected_size != actual_size) {
        res = EIO;
        goto err_fread;
    }

    res = katherine_px_config_load_bmc_data(px_config, buffer);

err_fread:
    free(buffer);    
err_buffer:
    fclose(file);
err_fopen:
    return res;
}

/**
 * Load pixel configuration from a BMC file contents (in BurdaMan format).
 * @param px_config Target configuration matrix.
 * @param bmc BMC file data.
 * @return Error code.
 */
int
katherine_px_config_load_bmc_data(katherine_px_config_t *px_config, const katherine_bmc_t *bmc)
{
    // Reset the pixel values.
    memset(&px_config->words, 0, 65536);

    int x, y;
    uint32_t *dest = (uint32_t*) px_config->words;
    const unsigned char *src = (const unsigned char *) bmc->px_config;

    // Parse data from BMC format.
    for (int i = 0; i < 65536; ++i) {
        x = i % 256;
        y = 255 - i / 256;
        dest[(64 * x) + (y >> 2)] |= (uint32_t) (src[i] << (8 * (3 - (y % 4))));
    }

    return 0;
}

/**
 * Load pixel configuration from a BPC file (in Pixet format).
 * @param px_config Target configuration matrix.
 * @param file_path BPC file path.
 * @return Error code.
 */
int
katherine_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path)
{
    int res = 0;

    const size_t expected_size = sizeof(katherine_bpc_t);
    FILE* file = fopen(file_path, "rb");
    if (file == NULL) {
        res = errno;
        goto err_fopen;
    }

    katherine_bpc_t *buffer = (katherine_bpc_t *) malloc(expected_size);
    if (buffer == NULL) {
        res = ENOMEM;
        goto err_buffer;
    }

    const size_t actual_size = fread(buffer, 1, expected_size, file);
    if (expected_size != actual_size) {
        res = EIO;
        goto err_fread;
    }

    res = katherine_px_config_load_bpc_data(px_config, buffer);

err_fread:
    free(buffer);    
err_buffer:
    fclose(file);
err_fopen:
    return res;
}

/**
 * Load pixel configuration from a BPC file contents (in Pixet format).
 * @param px_config Target configuration matrix.
 * @param bpc BPC file data.
 * @return Error code.
 */
int
katherine_px_config_load_bpc_data(katherine_px_config_t *px_config, const katherine_bpc_t *bpc)
{
    // Reset the pixel values.
    memset(&px_config->words, 0, 65536);

    int x, y;
    uint32_t *dest = (uint32_t*) px_config->words;
    const unsigned char *src = (const unsigned char *) bpc->px_config;
    static unsigned char reverse_array[] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
    unsigned char val;

    for (int i = 0; i < 65536; ++i) {
        y = i / 256;
        x = i % 256;
        val = (unsigned char) ((src[i] & 0x21) | (reverse_array[((src[i] & 0x1E) >> 1)] << 1));

        y = 255 - y;
        dest[(64 * x) + (y >> 2)] |= (uint32_t) (val << (8 * (3 - (y % 4))));
    }

    return 0;
}
