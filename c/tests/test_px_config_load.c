/**
 * \file
 * \brief Length checking of the BMC and BPC pixel-configuration loaders.
 *
 * Both formats are a bare 65536-byte matrix, one configuration byte per pixel
 * of the 256x256 sensor, with no header and no length field. The file's size
 * is therefore the only thing that says whether it belongs to this sensor at
 * all, which makes the length check the whole of the format validation.
 *
 * The check used to be one-sided. fread() was asked for exactly 65536 bytes
 * and its return value compared against that, so a short file failed but a
 * long one loaded with its tail ignored -- a matrix for a different sensor, or
 * in a different format, silently configuring the device wrongly. This freezes
 * both directions:
 *
 *   65535 bytes  short  -> KATHERINE_E_IO
 *   65536 bytes  exact  -> KATHERINE_E_OK
 *   65537 bytes  long   -> KATHERINE_E_IO
 *   0 bytes      empty  -> KATHERINE_E_IO
 *
 * A missing file is covered here too, being the neighbouring failure and free
 * to test. test_compat1.c already pins its 1.x errno translation; this checks
 * the 2.0 code itself.
 *
 * The success case also asserts that the matrix reached the packed words,
 * because a loader that returned KATHERINE_E_OK without copying anything
 * would satisfy every length assertion above. Where a given source byte lands
 * is deliberately not asserted -- writing the destination index out again
 * would restate the transpose rather than check it, and nothing else in the
 * suite pins that mapping either, so this test does not pretend to. The claim
 * here is only that something was written: a wholly zero file leaves the
 * words entirely zero, and one distinctive byte appears somewhere among
 * them.
 *
 * Plain, portable C with no sockets, so it builds and runs everywhere, like
 * test_dacs_validate.c. It does claim one resource, a scratch file, written
 * in the working directory CTest runs the binary in and removed again.
 *
 * \author Petr Mánek
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/error.h>
#include <katherine/px_config.h>

#include "ktest.h"

#define MATRIX_SIZE 65536

// Relative, so it lands in whatever working directory CTest runs this binary
// in: two build trees then cannot collide over it, as they would in a shared
// system temporary directory.
static const char *const PATH = "test_px_config_load.tmp";

/**
 * Write count bytes to the scratch path, all zero but for an optional marker
 * at offset 0.
 * \return Non-zero on success, so the caller can KT_REQUIRE it.
 */
static int
write_file(size_t count, int marker)
{
    FILE *f = fopen(PATH, "wb");
    if (f == NULL) return 0;

    int ok = 1;
    if (count > 0) {
        unsigned char *bytes = (unsigned char *) calloc(1, count);
        if (bytes == NULL) {
            fclose(f);
            return 0;
        }
        if (marker >= 0) bytes[0] = (unsigned char) marker;
        ok = fwrite(bytes, 1, count, f) == count;
        free(bytes);
    }

    return (fclose(f) == 0) && ok;
}

static void
test_exact_length_loads(void)
{
    KT_REQUIRE(write_file(MATRIX_SIZE, -1));

    katherine_px_config_t px_config;
    KT_CHECK_EQ(katherine_px_config_load_bmc_file(&px_config, PATH), KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_px_config_load_bpc_file(&px_config, PATH), KATHERINE_E_OK);

    remove(PATH);
}

static void
test_short_file_fails(void)
{
    KT_REQUIRE(write_file(MATRIX_SIZE - 1, -1));

    katherine_px_config_t px_config;
    KT_CHECK_EQ(katherine_px_config_load_bmc_file(&px_config, PATH), KATHERINE_E_IO);
    KT_CHECK_EQ(katherine_px_config_load_bpc_file(&px_config, PATH), KATHERINE_E_IO);

    remove(PATH);
}

// The case the one-sided check let through: every requested byte is there, so
// fread() is satisfied and only a look past the end can tell.
static void
test_long_file_fails(void)
{
    KT_REQUIRE(write_file(MATRIX_SIZE + 1, -1));

    katherine_px_config_t px_config;
    KT_CHECK_EQ(katherine_px_config_load_bmc_file(&px_config, PATH), KATHERINE_E_IO);
    KT_CHECK_EQ(katherine_px_config_load_bpc_file(&px_config, PATH), KATHERINE_E_IO);

    remove(PATH);
}

static void
test_empty_file_fails(void)
{
    KT_REQUIRE(write_file(0, -1));

    katherine_px_config_t px_config;
    KT_CHECK_EQ(katherine_px_config_load_bmc_file(&px_config, PATH), KATHERINE_E_IO);
    KT_CHECK_EQ(katherine_px_config_load_bpc_file(&px_config, PATH), KATHERINE_E_IO);

    remove(PATH);
}

static void
test_missing_file_fails(void)
{
    remove(PATH);

    katherine_px_config_t px_config;
    KT_CHECK_EQ(katherine_px_config_load_bmc_file(&px_config, "./no/such/directory/matrix.bmc"), KATHERINE_E_IO);
    KT_CHECK_EQ(katherine_px_config_load_bpc_file(&px_config, "./no/such/directory/matrix.bpc"), KATHERINE_E_IO);
}

// Guards against a loader that reports success without copying: an all-zero
// matrix must leave the packed words zero, and one distinctive byte must show
// up somewhere in them.
static void
test_success_copies_the_matrix(void)
{
    katherine_px_config_t px_config;

    KT_REQUIRE(write_file(MATRIX_SIZE, -1));
    memset(&px_config, 0xFF, sizeof(px_config));
    KT_REQUIRE(katherine_px_config_load_bmc_file(&px_config, PATH) == KATHERINE_E_OK);

    const unsigned char *bytes = (const unsigned char *) px_config.words;
    const size_t nbytes        = sizeof(px_config.words);

    int any_nonzero = 0;
    for (size_t i = 0; i < nbytes; ++i) {
        if (bytes[i] != 0) any_nonzero = 1;
    }
    KT_CHECK(!any_nonzero);

    KT_REQUIRE(write_file(MATRIX_SIZE, 0x5A));
    memset(&px_config, 0, sizeof(px_config));
    KT_REQUIRE(katherine_px_config_load_bmc_file(&px_config, PATH) == KATHERINE_E_OK);

    int marker_seen = 0;
    for (size_t i = 0; i < nbytes; ++i) {
        if (bytes[i] == 0x5A) marker_seen = 1;
    }
    KT_CHECK(marker_seen);

    remove(PATH);
}

int
main(void)
{
    KT_RUN(test_exact_length_loads);
    KT_RUN(test_short_file_fails);
    KT_RUN(test_long_file_fails);
    KT_RUN(test_empty_file_fails);
    KT_RUN(test_missing_file_fails);
    KT_RUN(test_success_copies_the_matrix);

    return kt_summary();
}
