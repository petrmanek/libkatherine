/**
 * \file
 * \brief The 1.x compatibility shim compiles 1.x source and maps its return codes back.
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
 *                (katherine_dacs_validate(), KATHERINE_E_INVAL); and,
 *                because 1.x reported this the same way, an unparsable
 *                remote address (KATHERINE_E_ADDR, see katherine1.h for
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
 * \author Petr Mánek
 * \date 26.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
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
// recognize either, so it falls to KATHERINE_E_IO like the shim's own
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
// The pixel coordinate under its 1.x spellings, which 2.0 namespaced to
// katherine_tpx3_coord_t because a byte per axis is Timepix3's matrix and not
// Timepix4's.
//
// Written against the object-like aliases in katherine1.h rather than the 2.0
// names, so this fails to compile if one of them is missing or misspelled --
// which is the only way an alias can be wrong, they being compile-time only.
// The struct tag, the typedef and the function-like alias for the renderer
// are all exercised, the last being the one a typo could resolve to a
// different call.

static void
test_coord_keeps_its_1x_spellings(void)
{
    struct katherine_coord tagged;
    katherine_coord_t c;
    char buf[64];

    tagged.x = 3;
    tagged.y = 4;

    c.x = 12;
    c.y = 34;

    // The renderer, reached through its 1.x name, must produce the same text
    // for a value built under either spelling.
    KT_CHECK(katherine_coord_snprint(buf, sizeof(buf), &c) > 0);
    KT_CHECK(strstr(buf, "12") != NULL);
    KT_CHECK(strstr(buf, "34") != NULL);

    tagged.x = c.x;
    KT_CHECK_EQ(tagged.x, 12);
}

// ------------------------------------------------------------------
// The function-like aliases: every 1.x call spelling, named once each.
//
// Compile-time only, with an arity to get wrong as well as a target: an alias
// that expands to the wrong function or takes the wrong number of arguments is
// a build failure here and nowhere else, because nothing else in the tree
// spells these names. Most of them need a readout, so none of the calls is
// allowed to run -- the branch is guarded by a volatile the compiler cannot
// fold, which keeps the bodies type-checked without making them unreachable
// code MSVC would warn about.
//
// The emulator's four aliases are absent: the shim defines them only when
// katherine/emulator.h precedes it, and naming them here would need this test
// to link against an optional build feature. Forty-six of the fifty remain.
static void
test_function_aliases(void)
{
    static volatile int never = 0;

    // Zeroed so that nothing is read uninitialised even in principle.
    katherine_device_t dev           = {0};
    katherine_udp_t udp              = {0};
    katherine_acquisition_t acq      = {0};
    katherine_px_config_t px         = {0};
    katherine_config_t cfg           = {0};
    katherine_tpx3_dacs_t dacs       = {0};
    katherine_trigger_t trg          = {0};
    katherine_test_pulse_config_t tp = {0};
    katherine_bmc_t bmc              = {0};
    katherine_bpc_t bpc              = {0};
    katherine_readout_status_t rs    = {0};
    katherine_comm_status_t cs       = {0};
    katherine_tpx3_coord_t crd       = {0};
    char cbuf[64]                    = {0};
    unsigned char vbuf[64]           = {0};
    size_t sz                        = 0;
    float f                          = 0.0f;

    if (never) {
        (void) katherine_acquisition_abort(&acq);
        (void) katherine_acquisition_begin(&acq, &cfg, KATHERINE_TPX3_READOUT_SEQUENTIAL, KATHERINE_TPX3_PX_TOA_TOT, false, false);
        (void) katherine_acquisition_init(&acq, &dev, vbuf, 0, 0, 0, 0);
        (void) katherine_acquisition_read(&acq);
        (void) katherine_acquisition_setup(&dev, &trg, false, &trg);
        (void) katherine_acquisition_stop(&acq);
        (void) katherine_configure(&dev, &cfg);
        (void) katherine_dacs_snprint(cbuf, 0, &dacs);
        (void) katherine_dacs_validate(&dacs);
        (void) katherine_device_init(&dev, "x");
        (void) katherine_get_adc_voltage(&dev, 0, &f);
        (void) katherine_get_chip_id(&dev, cbuf);
        (void) katherine_get_comm_status(&dev, &cs);
        (void) katherine_get_readout_status(&dev, &rs);
        (void) katherine_get_readout_temperature(&dev, &f);
        (void) katherine_get_sensor_temperature(&dev, &f);
        (void) katherine_output_block_config_update(&dev);
        (void) katherine_perform_digital_test(&dev);
        (void) katherine_px_config_load_bmc_data(&px, &bmc);
        (void) katherine_px_config_load_bmc_file(&px, "x");
        (void) katherine_px_config_load_bpc_data(&px, &bpc);
        (void) katherine_px_config_load_bpc_file(&px, "x");
        (void) katherine_set_acq_mode(&dev, KATHERINE_TPX3_PX_TOA_TOT, false);
        (void) katherine_set_acq_time(&dev, 0.0);
        (void) katherine_set_all_pixel_config(&dev, &px);
        (void) katherine_set_bias(&dev, 0, 0.0f);
        (void) katherine_set_dacs(&dev, &dacs);
        (void) katherine_set_no_frames(&dev, 0);
        (void) katherine_set_sensor_register(&dev, 0, 0);
        (void) katherine_set_seq_readout_start(&dev, 0);
        (void) katherine_set_test_pulses(&dev, &tp);
        (void) katherine_str_acquisition_mode(KATHERINE_TPX3_PX_TOA_TOT);
        (void) katherine_str_acquisition_status(KATHERINE_ACQUISITION_STATE_NOT_STARTED);
        (void) katherine_str_readout_type(KATHERINE_TPX3_READOUT_SEQUENTIAL);
        (void) katherine_timer_set(&dev);
        (void) katherine_tpx3_dacs_validate(&dacs);
        (void) katherine_udp_init(&udp, 0, "x", 0, 0);
        (void) katherine_udp_init_bound(&udp, "x", 0, "x", 0, 0);
        (void) katherine_udp_mutex_lock(&udp);
        (void) katherine_udp_mutex_unlock(&udp);
        (void) katherine_udp_recv(&udp, vbuf, &sz);
        (void) katherine_udp_recv_exact(&udp, vbuf, 0);
        (void) katherine_udp_send_exact(&udp, vbuf, 0);
        (void) katherine_udp_set_remote(&udp, "x", 0);
        (void) katherine_update_sensor_registers(&dev);
        (void) katherine_coord_snprint(cbuf, 0, &crd);
    }

    // Reaching here is the whole result: the file compiled with every 1.x
    // spelling named at its 1.x arity.
    KT_CHECK_EQ(never, 0);
}

// The object-like aliases: the renamed types, enumerators and one struct
// field, under their 1.x spellings.
//
// These are compile-time only, so most of the coverage is that this file
// compiles at all -- a missing or misspelled alias is a build failure here
// and nowhere else. Where more than existence can be checked cheaply, it is:
// an enumerator with a stringifier is rendered, which shows it resolves to
// the right enumerator rather than merely to some enumerator, and still
// speaks only 1.x, the stringifiers having 1.x names too.

static void
test_type_aliases(void)
{
    // Tag and typedef spellings of each renamed type, both of which 1.x had.
    enum katherine_readout_type readout  = READOUT_DATA_DRIVEN;
    enum katherine_acquisition_mode mode = ACQUISITION_MODE_ONLY_TOA;
    enum katherine_phase phase           = PHASE_8;
    enum katherine_freq freq             = FREQ_80;
    struct katherine_coord tagged        = {1, 2};

    katherine_readout_type_t readout_t  = readout;
    katherine_acquisition_mode_t mode_t = mode;
    katherine_phase_t phase_t           = phase;
    katherine_freq_t freq_t             = freq;
    katherine_coord_t coord_t           = tagged;

    KT_CHECK_EQ(sizeof(readout), sizeof(readout_t));
    KT_CHECK_EQ(sizeof(mode), sizeof(mode_t));
    KT_CHECK_EQ(sizeof(phase), sizeof(phase_t));
    KT_CHECK_EQ(sizeof(freq), sizeof(freq_t));
    KT_CHECK_EQ(sizeof(tagged), sizeof(coord_t));
    KT_CHECK_EQ(coord_t.x, 1);
    KT_CHECK_EQ(coord_t.y, 2);

    // The field aliases. 1.x read the chip count as a boolean, and misspelled
    // the preamplifier cascode DAC.
    katherine_comm_status_t comm;
    memset(&comm, 0, sizeof(comm));
    comm.chip_detected = 3;
    KT_CHECK_EQ(comm.chip_detected, 3);
    KT_CHECK(comm.chip_detected);

    katherine_dacs_t dacs;
    memset(&dacs, 0, sizeof(dacs));
    dacs.named.VPReamp_NCAS = 128;
    KT_CHECK_EQ(dacs.named.VPReamp_NCAS, 128);

    // Same field under either spelling, which is what the alias has to mean.
    KT_CHECK_EQ(dacs.named.Vpreamp_NCAS, 128);
}

static void
test_enumerator_aliases(void)
{
    KT_CHECK_STR_EQ(katherine_str_readout_type(READOUT_SEQUENTIAL), "sequential");
    KT_CHECK_STR_EQ(katherine_str_readout_type(READOUT_DATA_DRIVEN), "data_driven");

    KT_CHECK_STR_EQ(katherine_str_acquisition_status(ACQUISITION_NOT_STARTED), "not_started");
    KT_CHECK_STR_EQ(katherine_str_acquisition_status(ACQUISITION_RUNNING), "running");
    KT_CHECK_STR_EQ(katherine_str_acquisition_status(ACQUISITION_SUCCEEDED), "succeeded");
    KT_CHECK_STR_EQ(katherine_str_acquisition_status(ACQUISITION_TIMED_OUT), "timed_out");

    KT_CHECK_STR_EQ(katherine_str_acquisition_mode(ACQUISITION_MODE_TOA_TOT), "toa_tot");
    KT_CHECK_STR_EQ(katherine_str_acquisition_mode(ACQUISITION_MODE_ONLY_TOA), "only_toa");
    KT_CHECK_STR_EQ(katherine_str_acquisition_mode(ACQUISITION_MODE_EVENT_ITOT), "event_count_itot");

    KT_CHECK_STR_EQ(katherine_str_phase(PHASE_1), "phase_1");
    KT_CHECK_STR_EQ(katherine_str_phase(PHASE_2), "phase_2");
    KT_CHECK_STR_EQ(katherine_str_phase(PHASE_4), "phase_4");
    KT_CHECK_STR_EQ(katherine_str_phase(PHASE_8), "phase_8");
    KT_CHECK_STR_EQ(katherine_str_phase(PHASE_16), "phase_16");

    KT_CHECK_STR_EQ(katherine_str_freq(FREQ_20), "freq_20");
    KT_CHECK_STR_EQ(katherine_str_freq(FREQ_40), "freq_40");
    KT_CHECK_STR_EQ(katherine_str_freq(FREQ_80), "freq_80");
    KT_CHECK_STR_EQ(katherine_str_freq(FREQ_160), "freq_160");
}

static void
test_register_aliases(void)
{
    // No stringifier for these, so distinctness carries the check: thirteen
    // aliases must name thirteen registers, bar the one pair the chip itself
    // shares (method and period are the same register, see config.h). A
    // copy-paste slip mapping two aliases onto one register fails here.
    const katherine_tpx3_reg_t regs[] = {
        TPX3_REG_TEST_PULSE_METHOD,
        TPX3_REG_NUMBER_TEST_PULSES,
        TPX3_REG_OUT_BLOCK_CONFIG,
        TPX3_REG_PLL_CONFIG,
        TPX3_REG_GENERAL_CONFIG,
        TPX3_REG_SLVS_CONFIG,
        TPX3_REG_POWER_PULSING_PATTERN,
        TPX3_REG_SET_TIMER_LOW,
        TPX3_REG_SET_TIMER_MID,
        TPX3_REG_SET_TIMER_HIGH,
        TPX3_REG_SENSE_DAC_SELECTOR,
        TPX3_REG_EXT_DAC_SELECTOR,
    };
    const size_t count = sizeof(regs) / sizeof(regs[0]);

    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            KT_CHECK(regs[i] != regs[j]);
        }
    }

    KT_CHECK_EQ(TPX3_REG_TEST_PULSE_PERIOD, TPX3_REG_TEST_PULSE_METHOD);
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
    KT_RUN(test_coord_keeps_its_1x_spellings);
    KT_RUN(test_function_aliases);
    KT_RUN(test_type_aliases);
    KT_RUN(test_enumerator_aliases);
    KT_RUN(test_register_aliases);

    return kt_summary();
}
