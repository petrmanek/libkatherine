//
// Created by petr on 23.6.18.
//

#include <stdio.h>      // printf, sprintf
#include <string.h>     // strchr
#include <stdbool.h>    // bool
#include <stdlib.h>     // abort
#include <katherine/katherine.h>

static bool have_address = true;
static char address[16];
static int cur[] = {0, 0, 0, 0};
static int min[] = {192, 168, 1, 100};
static int max[] = {192, 168, 1, 200};

static int n_found = 0;
static int n_attempted = 0;

static void
test_failed(const char *address)
{
    /* Ignored */
}

static void
test_succeeded(const char *address, const char *chip_id)
{
    ++n_found;
    printf("Found device: %s,\t chip id: %s\n", address, chip_id);
}

static void
test_ip_address()
{
    ++n_attempted;

    int res;
    katherine_device_t device;

    res = katherine_device_init(&device, address);
    if (res) {
        test_failed(address);
        katherine_device_fini(&device);
        return;
    }

    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];

    res = katherine_get_chip_id(&device, chip_id);
    if (res) {
        test_failed(address);
    } else {
        test_succeeded(address, chip_id);
    }

    katherine_device_fini(&device);
}

static void
abort_parsing(char *argv[], const char *message)
{
    printf("%s: error parsing address range, %s\n", argv[0], message);
    abort();
}

static void
parse_range(int *min, int *max, char *range)
{
    char *dash_pos = strchr(range, '-');

    if (dash_pos == NULL) {
        // Expecting single number.
        *min = *max = atoi(range);
    } else {
        // Expecting two numbers separated by a dash.
        *dash_pos = '\0';
        *min = atoi(range);

        *dash_pos = '.';
        *max = atoi(dash_pos + 1);
    }
}

static void
parse_args(int argc, char *argv[], int min[4], int max[4])
{
    if (argc < 2) {
        return;
    }

    static const char separator = '.';
    char *arg = argv[1];
    char *pos[3];

    pos[0] = strchr(arg, separator);
    if (pos[0] == NULL) {
        abort_parsing(argv, "expected 3 dots (got 0)");
        return;
    }

    pos[1] = strchr(pos[0] + 1, separator);
    if (pos[1] == NULL) {
        abort_parsing(argv, "expected 3 dots (got 1)");
        return;
    }

    pos[2] = strchr(pos[1] + 1, separator);
    if (pos[2] == NULL) {
        abort_parsing(argv, "expected 3 dots (got 2)");
        return;
    }

    *pos[0] = '\0';
    parse_range(&min[0], &max[0], arg);

    *pos[1] = '\0';
    parse_range(&min[1], &max[1], pos[0] + 1);

    *pos[2] = '\0';
    parse_range(&min[2], &max[2], pos[1] + 1);

    parse_range(&min[3], &max[3], pos[2] + 1);
}

static void
next_address()
{
    int i = 3;

    while (++cur[i] > max[i]) {
        if (i > 0) {
            cur[i] = min[i];
            --i;
        } else {
            have_address = false;
            break;
        }
    }

    if (have_address) {
        sprintf(address, "%d.%d.%d.%d", cur[0], cur[1], cur[2], cur[3]);
    }
}

int
main(int argc, char *argv[])
{
    parse_args(argc, argv, min, max);

    int n_hosts = 1;
    for (int i = 0; i < 4; ++i) {
        n_hosts *= (max[i] - min[i] + 1);
    }

    printf("Testing %d hosts. Start: %d.%d.%d.%d, End: %d.%d.%d.%d\n", n_hosts,
        min[0], min[1], min[2], min[3], max[0], max[1], max[2], max[3]);

    memcpy(cur, min, sizeof(int) * 4);
    sprintf(address, "%d.%d.%d.%d", cur[0], cur[1], cur[2], cur[3]);

    while (have_address) {
        test_ip_address();
        next_address();
    }

    printf("Done. Tested %d hosts (%d found).\n", n_attempted, n_found);
    return n_found > 0 ? 0 : 1;
}
