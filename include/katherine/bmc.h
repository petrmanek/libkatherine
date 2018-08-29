//
// Created by petr on 9.6.18.
//

#ifndef THESIS_BMC_H
#define THESIS_BMC_H

/**
 * @file
 * @brief Functions related to the BMC matrix configuration format.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_bmc {
    char pconf[65536];

} katherine_bmc_t;

int
katherine_bmc_init(katherine_bmc_t *);

void
katherine_bmc_fini(katherine_bmc_t *);

int
katherine_bmc_load(katherine_bmc_t *, const char *);

#ifdef __cplusplus
}
#endif

#endif //THESIS_BMC_H
