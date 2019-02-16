/* Katherine Control Library
 *
 * This file was created on 9.6.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/**
 * @file
 * @brief Functions related to the BMC matrix configuration format.
 */

#include <katherine/global.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_bmc {
    char pconf[65536];

} katherine_bmc_t;

KATHERINE_EXPORTED int
katherine_bmc_load(katherine_bmc_t *bmc, const char *file_path);

#ifdef __cplusplus
}
#endif
