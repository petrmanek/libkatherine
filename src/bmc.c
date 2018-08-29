//
// Created by petr on 9.6.18.
//

#include <katherine/bmc.h>
#include <string.h>

/**
 * Initialize new BMC matrix.
 * @param bmc Matrix to initialize.
 * @return Error code.
 */
int
katherine_bmc_init(katherine_bmc_t *bmc)
{
    // Nothing done.
    return 0;
}

/**
 * Finalize BMC matrix.
 * @param bmc Matrix to finalize.
 */
void
katherine_bmc_fini(katherine_bmc_t *bmc)
{
    // Nothing done.
}

/**
 * Load matrix from a file.
 * @param bmc Target matrix.
 * @param data File path.
 * @return Error code.
 */
int
katherine_bmc_load(katherine_bmc_t *bmc, const char *data)
{
    // Reset the pixel values.
    memset(&bmc->pconf, 0, 65536);

    int x, y;
    int *buffer = (int*) bmc->pconf;

    // Parse data from BMC format.
    for (int i = 0; i < 65536; ++i) {
        x = i % 256;
        y = 255 - i / 256;
        buffer[(64 * x) + (y >> 2)] |= data[i] << (8 * (3 - (y % 4)));
    }

    return 0;
}
