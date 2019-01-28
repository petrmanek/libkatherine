//
// Created by petr on 14.6.18.
//

#ifndef THESIS_DEVICE_H
#define THESIS_DEVICE_H

/**
 * @file
 * @brief Functions related to Katherine.
 */

#include <katherine/udp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_device {
    katherine_udp_t control_socket;
    katherine_udp_t data_socket;
} katherine_device_t;

int
katherine_device_init(katherine_device_t *, const char *);

void
katherine_device_fini(katherine_device_t *);

#ifdef __cplusplus
}
#endif

#endif //THESIS_DEVICE_H
