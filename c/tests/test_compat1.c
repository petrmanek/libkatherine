/**
 * @file
 * @brief The 1.x compatibility shim compiles 1.x source and maps its return codes back.
 *
 * Everything below is written the way 1.x source was: only katherine1.h is
 * included (never a 2.0 header directly), every call uses a 1.x function
 * name, and every check compares the result against a positive `<errno.h>`
 * constant rather than a katherine_error_t enumerator -- proving that the
 * function-like-macro rename in katherine1.h resolves those names to the
 * shim's wrappers, not straight through to the 2.0 functions they wrap.
 *
 * One real failure is driven through each target of the shim's mapping
 * table (katherine1.h, katherine1_map_result()), by a distinct 2.0 failure
 * cause reachable without hardware:
 *
 *   - ETIMEDOUT: a receive on a bound-but-silent socket runs out its timeout.
 *   - EINVAL:    a DAC register value outside its chip field
 *                (katherine_dacs_validate(), -KATHERINE_E_INVAL); and,
 *                because 1.x reported this the same way, an unparsable
 *                remote address (-KATHERINE_E_ADDR, see katherine1.h for
 *                where that is attested against the 1.1.0 sources).
 *   - ENOMEM:    an acquisition buffer too large for any allocator to satisfy.
 *   - EIO:       every katherine_error_t enumerator this shim does not name
 *                explicitly, reached here via a missing pixel-configuration
 *                file (fopen() failure not otherwise recognized).
 *
 * A negative control closes each end: a DAC vector that passes validation,
 * and a real send/receive round trip over loopback, both returning 0.
 *
 * All sockets bind uncommon high loopback ports of their own -- distinct
 * from every other test's -- so this claims no global resource, exactly
 * like test_udp_pinning.c and test_dacs_validate.c, whose fixtures this
 * borrows.
 *
 * @author Petr Mánek
 * @date 26.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <katherine/katherine1.h>

#include "ktest.h"

#define HOST            "127.0.0.1"
#define PORT_SILENT     43700 /* bound, never sent to: the timeout case */
#define PORT_BADADDR    43703 /* bound only long enough to fail resolving its remote */
#define PORT_A          43701 /* the round-trip pair */
#define PORT_B          43702
#define TIMEOUT_MS      100

#define ROUND_TRIP_TEXT "compat1"

// ------------------------------------------------------------------
// ETIMEDOUT: a receive on a bound-but-silent socket runs out its timeout,
// the same condition test_udp_pinning.c drives via an idle peer.

static void
test_recv_timeout_maps_to_etimedout(void)
{
    katherine_udp_t u;
    KT_REQUIRE(katherine_udp_init_bound(&u, HOST, PORT_SILENT, HOST, PORT_SILENT + 1, TIMEOUT_MS) == 0);

    char buf[64];
    size_t count = sizeof(buf);
    KT_CHECK_EQ(katherine_udp_recv(&u, buf, &count), ETIMEDOUT);

    katherine_udp_fini(&u);
}

// ------------------------------------------------------------------
// EINVAL, cause 1: a DAC register outside its chip field. Vfbk (index 4) is
// 8 bits wide (Tpx3 manual Table 11, see test_dacs_validate.c), so 256 is
// one past its maximum; every other register is left at zero.

static void
test_dacs_validate_out_of_range_maps_to_einval(void)
{
    katherine_dacs_t dacs = {0};
    dacs.named.Vfbk       = 256;
    KT_CHECK_EQ(katherine_dacs_validate(&dacs), EINVAL);
}

// ------------------------------------------------------------------
// EINVAL, cause 2: an address inet_pton() cannot parse. 1.x reported this
// as EINVAL too (see katherine1.h for the 1.1.0 evidence); 2.0 names it
// KATHERINE_E_ADDR, a separate enumerator this shim maps onto the same
// 1.x code, so this case is a distinct failure from the one above with
// the same expected result.

static void
test_udp_bad_remote_address_maps_to_einval(void)
{
    katherine_udp_t u;
    KT_CHECK_EQ(katherine_udp_init_bound(&u, NULL, PORT_BADADDR, "not-an-address", 0, TIMEOUT_MS), EINVAL);
}

// ------------------------------------------------------------------
// ENOMEM: an md_buffer_size no allocator can satisfy. The device pointer is
// never dereferenced by katherine_acquisition_init() -- only stored -- so
// NULL is safe here; the allocation fails before anything else runs.

static void
test_acquisition_init_huge_buffer_maps_to_enomem(void)
{
    katherine_acquisition_t acq;
    size_t huge = SIZE_MAX - 4096; /* leaves room for the buffer's +sizeof(uint64_t) headroom to not wrap */
    KT_CHECK_EQ(katherine_acquisition_init(&acq, NULL, NULL, huge, 1, 0, 0), ENOMEM);
}

// ------------------------------------------------------------------
// EIO: every katherine_error_t enumerator this shim does not name
// explicitly. A pixel-configuration file that does not exist fails
// fopen() with a code the library's own map_fopen_errno() does not
// recognize either, so it falls to -KATHERINE_E_IO like the shim's own
// default case.

static void
test_px_config_missing_file_maps_to_eio(void)
{
    static katherine_px_config_t px_config; /* static: too large for a comfortable stack frame */
    KT_CHECK_EQ(katherine_px_config_load_bmc_file(&px_config, "/nonexistent/path/libkatherine-test_compat1.bmc"), EIO);
}

// ------------------------------------------------------------------
// Negative control, cause 1: an all-zero DAC vector is within every
// register's range.

static void
test_dacs_validate_success(void)
{
    katherine_dacs_t dacs;
    memset(&dacs, 0, sizeof(dacs));
    KT_CHECK_EQ(katherine_dacs_validate(&dacs), 0);
}

// ------------------------------------------------------------------
// Negative control, cause 2: a real send/receive round trip over loopback,
// exercising the 1.x names of the send and receive paths together rather
// than a single call in isolation.

static void
test_udp_round_trip_success(void)
{
    katherine_udp_t a, b;
    KT_REQUIRE(katherine_udp_init_bound(&a, HOST, PORT_A, HOST, PORT_B, TIMEOUT_MS) == 0);
    KT_REQUIRE(katherine_udp_init_bound(&b, HOST, PORT_B, HOST, PORT_A, TIMEOUT_MS) == 0);

    KT_CHECK_EQ(katherine_udp_send_exact(&a, ROUND_TRIP_TEXT, strlen(ROUND_TRIP_TEXT)), 0);

    char buf[64];
    size_t count = sizeof(buf);
    KT_CHECK_EQ(katherine_udp_recv(&b, buf, &count), 0);
    KT_CHECK_EQ(count, strlen(ROUND_TRIP_TEXT));
    KT_CHECK_MEM_EQ(buf, ROUND_TRIP_TEXT, count);

    katherine_udp_fini(&b);
    katherine_udp_fini(&a);
}

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_recv_timeout_maps_to_etimedout);
    KT_RUN(test_dacs_validate_out_of_range_maps_to_einval);
    KT_RUN(test_udp_bad_remote_address_maps_to_einval);
    KT_RUN(test_acquisition_init_huge_buffer_maps_to_enomem);
    KT_RUN(test_px_config_missing_file_maps_to_eio);
    KT_RUN(test_dacs_validate_success);
    KT_RUN(test_udp_round_trip_success);

    return kt_summary();
}
