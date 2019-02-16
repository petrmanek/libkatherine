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
#include <malloc.h>
#include <errno.h>
#include <katherine/bmc.h>

/**
 * Load matrix from a file.
 * @param bmc Target matrix.
 * @param file_path File path.
 * @return Error code.
 */
int
katherine_bmc_load(katherine_bmc_t *bmc, const char *file_path)
{
    int res = 0;

    const size_t expected_size = 65536;
    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        res = errno;
        goto err_fopen;
    }

    char* buffer = (char *) malloc(expected_size);
    if (buffer == NULL) {
        res = ENOMEM;
        goto err_buffer;
    }

    const size_t actual_size = fread(buffer, 1, expected_size, file);
    if (expected_size != actual_size) {
        res = EIO;
        goto err_fread;
    }

    // Reset the pixel values.
    memset(&bmc->pconf, 0, 65536);

    int x, y;
    int *dest = (int*) bmc->pconf;

    // Parse data from BMC format.
    for (int i = 0; i < 65536; ++i) {
        x = i % 256;
        y = 255 - i / 256;
        dest[(64 * x) + (y >> 2)] |= buffer[i] << (8 * (3 - (y % 4)));
    }

err_fread:
    free(buffer);    
err_buffer:
    fclose(file);
err_fopen:
    return res;
}
