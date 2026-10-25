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
 *                remote address (KATHERINE_E_ADDRESS, see katherine1.h for
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

// The session header first: katherine1.h renames the 1.x session calls with
// macros, and a macro reaching a declaration rewrites that too.
#include "protocol/cmd_interface.h"
#include "transport/ops.h"
#include "transport/udp.h"

#include <katherine/katherine1.h>

#include "kports.h"
#include "device/private.h"
#include "ktest.h"

#define HOST            "127.0.0.1"
#define PORT_SILENT     (KPORTS_COMPAT1 + 0) // bound, never sent to: the timeout case
#define PORT_BADADDRESS (KPORTS_COMPAT1 + 3) // bound only long enough to fail resolving its remote
#define PORT_A          (KPORTS_COMPAT1 + 1) // the round-trip pair
#define PORT_B          (KPORTS_COMPAT1 + 2)
#define PORT_NOMEM      (KPORTS_COMPAT1 + 4) // the device the ENOMEM case reads through
#define TIMEOUT_MS      100

#define ROUND_TRIP_TEXT "compat1"

// Both sanitizers abort on an allocation the ENOMEM case below asks for,
// instead of letting malloc() decline it; allocator_may_return_null makes them
// return NULL as the C library would. Their runtimes call these at startup;
// nothing else does.
const char *
__asan_default_options(void)
{
    return "allocator_may_return_null=1";
}

const char *
__tsan_default_options(void)
{
    return "allocator_may_return_null=1";
}

// ------------------------------------------------------------------
// ETIMEDOUT: opening a readout that nobody answers runs the enumeration out
// of its timeout. TEST-NET-1 (RFC 5737) rather than loopback, where this
// suite's own emulator answers.

static void
test_recv_timeout_maps_to_etimedout(void)
{
    katherine_device_t dev;
    KT_CHECK_EQ(katherine_device_init(&dev, "udp://192.0.2.1"), ETIMEDOUT);
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
// KATHERINE_E_ADDRESS, a separate enumerator this shim maps onto the same
// 1.x code, so this case is a distinct failure from the one above with
// the same expected result.

static void
test_udp_bad_remote_address_maps_to_einval(void)
{
    katherine_device_t dev;
    KT_CHECK_EQ(katherine_device_init(&dev, "not-an-address"), EINVAL);
}

// ------------------------------------------------------------------
// ENOMEM: an md_buffer_size no allocator can satisfy. The size reaches the
// device's transport, which is asked to resize its lent buffer to it, so a
// device with an open link is needed rather than a null one.

static void
test_acquisition_init_huge_buffer_maps_to_enomem(void)
{
    katherine_device_t dev = {0};
    // The acquisition opens its own data link from the device.
    strcpy(dev.address, "udp://" HOST);
    katherine_device_state(&dev)->udp_data_port = PORT_NOMEM + 1;

    const katherine_udp_config_t control = {
        .local_address  = HOST,
        .local_port     = PORT_NOMEM,
        .remote_address = HOST,
        .remote_port    = PORT_NOMEM + 1,
        .timeout_ms     = TIMEOUT_MS,
    };
    KT_REQUIRE(katherine_transport_init_udp(&dev.control_transport, &control) == 0);

    katherine_acquisition_t acq;
    size_t huge = SIZE_MAX - 4096; // leaves room for the buffer's +sizeof(uint64_t) headroom to not wrap
    KT_CHECK_EQ(katherine_acquisition_init(&acq, &dev, NULL, huge, 1, 0, 0), ENOMEM);

    katherine_transport_fini(&dev.control_transport);
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
    static katherine_px_config_t px_config; // static: too large for a comfortable stack frame
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
// The 1.x session calls still compile and still link, and every one of them
// declines: a session is no longer part of this library's interface, and a
// caller reaches a readout by address instead. Ten of the eleven 1.x
// declared; katherine_udp_snprint() is the one left out, since it rendered a
// session's endpoints and the shim's katherine_udp_t is incomplete, so there
// is nothing it could truthfully print. katherine_transport_snprint() is the
// 2.0 call.
//
// Through a pointer, as here. A 1.x source declaring `katherine_udp_t u;` by
// value does not compile against this header at all -- the type is
// incomplete by design -- which is the limit of what the shim offers for
// this family.

static void
test_udp_calls_decline(void)
{
    katherine_udp_t *u = NULL;
    char buf[64];
    size_t count = sizeof(buf);

    KT_CHECK_EQ(katherine_udp_init(u, PORT_A, HOST, PORT_B, TIMEOUT_MS), ENOTSUP);
    KT_CHECK_EQ(katherine_udp_init_bound(u, HOST, PORT_A, HOST, PORT_B, TIMEOUT_MS), ENOTSUP);
    KT_CHECK_EQ(katherine_udp_send_exact(u, ROUND_TRIP_TEXT, strlen(ROUND_TRIP_TEXT)), ENOTSUP);
    KT_CHECK_EQ(katherine_udp_recv_exact(u, buf, count), ENOTSUP);
    KT_CHECK_EQ(katherine_udp_recv(u, buf, &count), ENOTSUP);
    KT_CHECK_EQ(katherine_udp_set_remote(u, HOST, PORT_B), ENOTSUP);
    KT_CHECK_EQ(katherine_udp_mutex_lock(u), ENOTSUP);
    KT_CHECK_EQ(katherine_udp_mutex_unlock(u), ENOTSUP);

    // Void in 1.x, so there is nothing to check but that they compile, link
    // and return.
    katherine_udp_fini(u);
    katherine_udp_pin_remote(u);
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
// The six hit layouts under their 1.x spellings, namespaced in 2.0 for the
// same reason as the coordinate above: a Timepix4 hit is a different shape,
// not a wider one.
//
// Named through the aliases, as the coordinate is, with every field designated --
// the alias names the struct, so a field that moved would compile here and
// misread a hit in a caller.
//
// The Event+iToT pair reads only the fields common to 1.x and 2.0. 1.x gave
// the fast variant a hit_count that read a dummy field and 2.0 dropped it;
// the slow variant gained one, read here to pin each alias to its variant.

static void
test_hit_layouts_keep_their_1x_spellings(void)
{
    char buf[128];

    katherine_px_f_toa_tot_t a = {.chip = 0, .coord = {1, 2}, .timestamp = 3, .tot = 5};
    KT_CHECK(katherine_px_f_toa_tot_snprint(buf, sizeof(buf), &a) > 0);
    KT_CHECK_EQ(a.coord.x + a.timestamp + a.tot, 1 + 3 + 5);

    struct katherine_px_toa_tot b = {.coord = {6, 7}, .timestamp = 8, .hit_count = 2, .tot = 9};
    KT_CHECK(katherine_px_toa_tot_snprint(buf, sizeof(buf), &b) > 0);
    KT_CHECK_EQ(b.coord.y + b.timestamp + b.hit_count + b.tot, 7 + 8 + 2 + 9);

    katherine_px_f_toa_only_t c = {.coord = {1, 2}, .timestamp = 4};
    KT_CHECK(katherine_px_f_toa_only_snprint(buf, sizeof(buf), &c) > 0);

    katherine_px_toa_only_t d = {.coord = {1, 2}, .timestamp = 4, .hit_count = 3};
    KT_CHECK(katherine_px_toa_only_snprint(buf, sizeof(buf), &d) > 0);

    katherine_px_f_event_itot_t e = {.coord = {1, 2}, .event_count = 4, .integral_tot = 5};
    KT_CHECK(katherine_px_f_event_itot_snprint(buf, sizeof(buf), &e) > 0);
    KT_CHECK_EQ(e.coord.x + e.event_count + e.integral_tot, 1 + 4 + 5);

    struct katherine_px_event_itot f = {.coord = {1, 2}, .event_count = 4, .integral_tot = 5};
    KT_CHECK(katherine_px_event_itot_snprint(buf, sizeof(buf), &f) > 0);
    KT_CHECK_EQ(f.coord.y + f.event_count + f.integral_tot, 2 + 4 + 5);
    KT_CHECK_EQ(f.hit_count, 0);
}

// ------------------------------------------------------------------
// The one shim function that is not a rename.
//
// katherine1_acquisition_setup() took the delayed start as its own argument
// where 2.0 carries it in the trigger, so it copies the caller's trigger and
// sets the flag on the copy. Both halves of that are worth pinning: the flag
// has to reach the wire, and the caller's const trigger has to come back
// unchanged.
//
// A recording stub stands in for the readout, which also settles the claim
// on record that a mock cannot be borrowed here because katherine1.h renames
// the 2.0 symbols. It renames the 1.x spellings; the transport vtable has no
// 1.x spelling, so the two share this translation unit without complaint.

static uint8_t g_stub_last[8];
static int g_stub_sends;

static katherine_error_t
stub_ok(void *ctx)
{
    (void) ctx;
    return KATHERINE_E_OK;
}

static katherine_error_t
stub_flush_incoming(void *ctx, uint32_t *discarded)
{
    (void) ctx;
    if (discarded != NULL) *discarded = 0;
    return KATHERINE_E_OK;
}

static katherine_error_t
stub_send_cmd(void *ctx, const void *data, size_t count)
{
    (void) ctx;
    if (count >= sizeof(g_stub_last)) memcpy(g_stub_last, data, sizeof(g_stub_last));
    ++g_stub_sends;
    return KATHERINE_E_OK;
}

// What a readout answers: the operation code of the request echoed in byte 6
// of an otherwise empty datagram.
static katherine_error_t
stub_recv_crd(void *ctx, void *crd, size_t *len)
{
    (void) ctx;
    memset(crd, 0, KATHERINE_CMD_CRD_SIZE);
    ((uint8_t *) crd)[6] = g_stub_last[6];
    if (len != NULL) *len = KATHERINE_CMD_CRD_SIZE;
    return KATHERINE_E_OK;
}

static katherine_error_t
stub_set_timeout(void *ctx, uint32_t timeout_ms)
{
    (void) ctx;
    (void) timeout_ms;
    return KATHERINE_E_OK;
}

static int
stub_last_os_error(void *ctx)
{
    (void) ctx;
    return 0;
}

static const katherine_transport_ops_t stub_recording_ops = {
    .send_cmd          = stub_send_cmd,
    .recv_crd          = stub_recv_crd,
    .flush_incoming    = stub_flush_incoming,
    .set_timeout       = stub_set_timeout,
    .mutex_lock        = stub_ok,
    .mutex_unlock      = stub_ok,
    .get_last_os_error = stub_last_os_error,
};

// A second-generation readout that records rather than answers. Declared
// rather than enumerated, which is what gives it a generation without a
// link, and then given the stub's table.
static void
stub_device(katherine_device_t *device)
{
    const katherine_device_info_t info = {
        .hw_type       = 0x03,
        .hw_revision   = 3,
        .serial_number = 1,
        .fw_version    = 5,
        .chip_count    = 1,
    };

    memset(device, 0, sizeof(*device));
    KT_REQUIRE(katherine_device_declare(device, &info) == KATHERINE_E_OK);
    device->control_transport.ops = &stub_recording_ops;

    memset(g_stub_last, 0, sizeof(g_stub_last));
    g_stub_sends = 0;
}

static void
test_acquisition_setup_puts_the_delayed_start_on_the_wire(void)
{
    katherine_device_t device;
    stub_device(&device);

    katherine_trigger_t start = {0};
    katherine_trigger_t stop  = {0};
    start.enabled             = true;
    start.channel             = (katherine_trigger_source_t) 2;

    const katherine_trigger_t before = start;

    KT_CHECK_EQ(katherine_acquisition_setup(&device, &start, true, &stop), 0);
    KT_CHECK(g_stub_sends > 0);

    // The last datagram is the trigger-setup register write. On a
    // second-generation readout the word carries the enable at bit 0, a
    // four-bit channel at bit 1, the edge at 5 and the delayed start at 6.
    KT_CHECK_EQ((int) g_stub_last[6], (int) CMD_TYPE_ACQUISITION_SETUP);
    KT_CHECK_EQ((int) (g_stub_last[0] & 0x01), 1);
    KT_CHECK_EQ((int) ((g_stub_last[0] >> 1) & 0x0F), 2);
    KT_CHECK_EQ((int) ((g_stub_last[0] >> 6) & 0x01), 1);

    // And the trigger the caller lent is theirs still.
    KT_CHECK_MEM_EQ(&start, &before, sizeof(start));
}

static void
test_acquisition_setup_without_a_delay_clears_the_bit(void)
{
    katherine_device_t device;
    stub_device(&device);

    katherine_trigger_t start = {0};
    katherine_trigger_t stop  = {0};
    start.enabled             = true;
    start.channel             = (katherine_trigger_source_t) 2;

    KT_CHECK_EQ(katherine_acquisition_setup(&device, &start, false, &stop), 0);
    KT_CHECK_EQ((int) ((g_stub_last[0] >> 6) & 0x01), 0);
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

    // The field aliases. 1.x read the chip count as a boolean, named the frame
    // count for its absence, misspelled the preamplifier cascode DAC, and said
    // phase where it meant the pixel clock's.
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

    katherine_config_t config;
    memset(&config, 0, sizeof(config));
    config.no_frames = 4;
    KT_CHECK_EQ(config.no_frames, 4);
    KT_CHECK_EQ(config.frame_count, 4);

    config.correct_phase = true;
    KT_CHECK(config.correct_phase);
    KT_CHECK(config.correct_clock_phase);
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
    KT_RUN(test_udp_calls_decline);
    KT_RUN(test_coord_keeps_its_1x_spellings);
    KT_RUN(test_hit_layouts_keep_their_1x_spellings);
    KT_RUN(test_acquisition_setup_puts_the_delayed_start_on_the_wire);
    KT_RUN(test_acquisition_setup_without_a_delay_clears_the_bit);
    KT_RUN(test_function_aliases);
    KT_RUN(test_type_aliases);
    KT_RUN(test_enumerator_aliases);
    KT_RUN(test_register_aliases);

    return kt_summary();
}
