//
// Created by petr on 9.6.18.
//

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
katherine_bmc_load(katherine_bmc_t *, const char *);

#ifdef __cplusplus
}
#endif
