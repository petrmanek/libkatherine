/**
 * \file
 * \brief The USB backend's ring of outstanding reads, driven without a bus.
 *
 * A bulk endpoint needs hardware, so the backend's ring had no coverage at all
 * and its failure modes are the quiet kind: a slot that stops being reposted
 * shrinks the ring one transfer at a time and the stream dies minutes later,
 * looking like a readout that went silent. The cases below drive that logic
 * through a stand-in libusb that lets the test say when each transfer comes
 * back and with what.
 *
 * What is NOT covered here is throughput, which is the whole point of the ring
 * and needs a readout; see c/benchmarks/bench_usb.c.
 *
 * \author Petr Mánek
 * \date 15.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include <katherine/error.h>
#include <katherine/transport.h>
#include "transport/factory.h"
#include "transport/usb.h"

#include "libusb.h"

#include "ktest.h"

#define RING       4
#define BUF_BYTES  1024
#define TIMEOUT_MS 20

static katherine_usb_config_t
config_for(katherine_usb_stream_t stream)
{
    katherine_usb_config_t config = katherine_usb_config_default();
    config.stream                 = stream;
    config.queue_len              = RING;
    config.buffer_size            = BUF_BYTES;
    config.timeout_ms             = TIMEOUT_MS;

    // No port chain: the one attached readout, which every case here sets up.
    config.port_count = 0;

    return config;
}

// ------------------------------------------------------------------

// katherine_usb_inflight() reports through an out-parameter, so the checks
// below read it through this.
static uint32_t
inflight(const katherine_transport_t *t)
{
    uint32_t n = 0;
    KT_CHECK_EQ(katherine_usb_inflight(t, &n), KATHERINE_E_OK);

    return n;
}

/* Opening fills the ring: every read is posted up front, which is what keeps
 * the bus busy before the caller has asked for anything. */
static void
test_opening_posts_the_whole_ring(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    KT_CHECK_EQ(fake_usb_submitted(), RING);
    KT_CHECK_EQ(inflight(&t), (uint32_t) RING);

    katherine_transport_fini(&t);
}

/* A buffer that came back with data is lent out whole, and giving it back
 * posts the read again -- which is what keeps the ring full. */
static void
test_a_returned_buffer_is_posted_again(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    const int submitted_at_open = fake_usb_submit_count();
    KT_REQUIRE(fake_usb_complete_oldest(LIBUSB_TRANSFER_COMPLETED, 512) == 0);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_OK);
    KT_CHECK_EQ(len, (size_t) 512);
    KT_CHECK(buf != NULL);

    // Lent, so one fewer is outstanding until it comes back.
    KT_CHECK_EQ(inflight(&t), (uint32_t) (RING - 1));

    KT_CHECK_EQ(katherine_transport_release_data_buf(&t, buf), KATHERINE_E_OK);
    KT_CHECK_EQ(inflight(&t), (uint32_t) RING);
    KT_CHECK_EQ(fake_usb_submit_count(), submitted_at_open + 1);

    katherine_transport_fini(&t);
}

/* The case that fails quietly: a read that comes back with nothing, which an
 * idle readout produces once per timeout on every transfer in the ring. If
 * those are not posted again the ring drains away and the stream never
 * resumes -- so acquiring again must find the ring full, not empty. */
static void
test_an_empty_completion_does_not_shrink_the_ring(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    // Every transfer times out empty, as they would against an idle readout.
    for (int i = 0; i < RING; ++i)
        KT_REQUIRE(fake_usb_complete_oldest(LIBUSB_TRANSFER_TIMED_OUT, 0) == 0);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_TIMEOUT);

    // The ring is whole again, so the next datum has somewhere to land.
    KT_CHECK_EQ(inflight(&t), (uint32_t) RING);

    // And it does: one more completion, with data this time, is delivered.
    KT_REQUIRE(fake_usb_complete_oldest(LIBUSB_TRANSFER_COMPLETED, 64) == 0);
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_OK);
    KT_CHECK_EQ(len, (size_t) 64);

    katherine_transport_fini(&t);
}

/* A transfer that timed out with bytes in it carries measurement data like
 * any other, and is lent rather than discarded. */
static void
test_a_short_timeout_still_carries_data(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    KT_REQUIRE(fake_usb_complete_oldest(LIBUSB_TRANSFER_TIMED_OUT, 48) == 0);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_OK);
    KT_CHECK_EQ(len, (size_t) 48);

    katherine_transport_fini(&t);
}

/* Silence is a timeout rather than a zero-length buffer: a caller told it was
 * lent nothing would decode whatever the buffer held last. */
static void
test_silence_times_out(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_TIMEOUT);

    katherine_transport_fini(&t);
}

/* Returning something the transport never lent is refused: on a ring it would
 * post a transfer that is already outstanding. */
static void
test_returning_a_foreign_buffer_is_refused(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    char elsewhere[8];
    KT_CHECK_EQ(katherine_transport_release_data_buf(&t, elsewhere), KATHERINE_E_INVAL);

    katherine_transport_fini(&t);
}

/* Closing cancels what is outstanding and then waits for it: cancellation is
 * a request, and freeing the ring before the transfers come back would free
 * buffers libusb still owns. */
static void
test_closing_waits_for_every_transfer(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);
    KT_REQUIRE(fake_usb_submitted() == RING);

    katherine_transport_fini(&t);

    KT_CHECK_EQ(fake_usb_submitted(), 0);
}

/* A readout that goes away fails every repost, and saying so beats waiting out
 * the timeout to report silence. */
static void
test_a_failing_repost_is_reported(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    KT_REQUIRE(fake_usb_complete_oldest(LIBUSB_TRANSFER_TIMED_OUT, 0) == 0);

    void *buf  = NULL;
    size_t len = 0;
    fake_usb_fail_next_submit(LIBUSB_ERROR_NO_DEVICE);
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_CLOSED);

    katherine_transport_fini(&t);
}

/* The synchronous shape keeps one read at a time: it posts nothing on open and
 * issues exactly one transfer per acquire, on the data endpoint. */
static void
test_the_synchronous_shape_reads_on_demand(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_SYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    KT_CHECK_EQ(fake_usb_submitted(), 0);
    KT_CHECK_EQ(fake_usb_bulk_count(), 0);

    fake_usb_set_bulk_result(LIBUSB_SUCCESS, 256);

    void *buf  = NULL;
    size_t len = 0;
    KT_CHECK_EQ(katherine_transport_acquire_data_buf(&t, &buf, &len), KATHERINE_E_OK);
    KT_CHECK_EQ(len, (size_t) 256);
    KT_CHECK_EQ(fake_usb_bulk_count(), 1);
    // The data endpoint is the library's constant, not a setting.
    KT_CHECK_EQ(fake_usb_last_bulk_endpoint(), 0x81);

    // Giving it back costs nothing, there being no ring to refill.
    KT_CHECK_EQ(katherine_transport_release_data_buf(&t, buf), KATHERINE_E_OK);
    KT_CHECK_EQ(fake_usb_bulk_count(), 1);

    katherine_transport_fini(&t);
}

/* A device that is not on the bus is reported as such, before any ring is
 * built. */
static void
test_an_absent_device_is_reported(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);
    fake_usb_set_present(0);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &config), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(fake_usb_submitted(), 0);
}

/* A ring that cannot be posted at all leaves nothing outstanding: the open
 * fails and unwinds what it built. */
static void
test_a_ring_that_cannot_be_posted_unwinds(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);
    fake_usb_fail_next_submit(LIBUSB_ERROR_NO_DEVICE);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &config), KATHERINE_E_CLOSED);
    KT_CHECK_EQ(fake_usb_submitted(), 0);
}

/* A transport is the caller's to allocate, so opening one has to define all
 * of it. An unwritten strict_ack decides from stack contents whether a command
 * response must repeat its request's opcode. */
static void
test_opening_defines_every_field(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    memset(&t, 0xFF, sizeof(t));

    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    KT_CHECK(!t.strict_ack);
    KT_CHECK_EQ(t.stray_command_responses, (uint64_t) 0);

    katherine_transport_fini(&t);
}

/* A message transport carries commands and nothing else: it builds no ring,
 * and its table leaves out the operations belonging to the other role.
 * Checked on the table rather than by calling, those entries being absent
 * precisely so that asking is a programming error and not an answer every
 * command in the library would have to document. */
static void
test_a_message_transport_lends_no_buffers(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    // Both endpoints and no ring: the shape that exchanges messages.
    katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    config.ep_out                 = KATHERINE_USB_EP_CMD_OUT;
    config.ep_in                  = KATHERINE_USB_EP_CMD_IN;
    config.queue_len              = 0;

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    /* Nothing was posted, there being no ring to post. */
    KT_CHECK_EQ(fake_usb_submitted(), 0);

    KT_CHECK(t.ops->acquire_data_buf == NULL);
    KT_CHECK(t.ops->release_data_buf == NULL);
    KT_CHECK(t.ops->set_buffer_size == NULL);

    /* And it does play its own. */
    KT_CHECK(t.ops->send_cmd != NULL);
    KT_CHECK(t.ops->recv_crd != NULL);
    KT_CHECK(t.ops->flush_incoming != NULL);

    katherine_transport_fini(&t);
}

/* And the other way round: a stream transport has no OUT endpoint. */
static void
test_a_stream_transport_sends_nothing(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t t;
    const katherine_usb_config_t config = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&t, &config) == KATHERINE_E_OK);

    KT_CHECK(t.ops->send_cmd == NULL);
    KT_CHECK(t.ops->recv_crd == NULL);
    KT_CHECK(t.ops->flush_incoming == NULL);

    KT_CHECK(t.ops->acquire_data_buf != NULL);
    KT_CHECK(t.ops->release_data_buf != NULL);

    katherine_transport_fini(&t);
}

/* An interface is one handle's at a time, so a second transport opened on its
 * own is refused -- the condition measured on the readout as
 * LIBUSB_ERROR_BUSY, which this backend reports as an I/O failure. */
static void
test_a_second_independent_open_is_refused(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t first;
    const katherine_usb_config_t stream = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_transport_init_usb(&first, &stream) == KATHERINE_E_OK);
    KT_CHECK_EQ(fake_usb_claims(), 1);

    katherine_usb_config_t messages = config_for(KATHERINE_USB_STREAM_ASYNC);
    messages.ep_out                 = KATHERINE_USB_EP_CMD_OUT;
    messages.ep_in                  = KATHERINE_USB_EP_CMD_IN;
    messages.queue_len              = 0;

    katherine_transport_t second;
    KT_CHECK_EQ(katherine_transport_init_usb(&second, &messages), KATHERINE_E_IO);
    KT_CHECK_EQ(fake_usb_claims(), 1);

    katherine_transport_fini(&first);
}

/* Handed the readout the first transport opened, the second drives it without
 * claiming again, and closes nothing: the half that opened is the half that
 * closes, which is why the other is finalised first. */
static void
test_a_second_transport_adopts_the_first(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_usb_config_t messages = config_for(KATHERINE_USB_STREAM_ASYNC);
    messages.ep_out                 = KATHERINE_USB_EP_CMD_OUT;
    messages.ep_in                  = KATHERINE_USB_EP_CMD_IN;
    messages.queue_len              = 0;

    katherine_transport_t control;
    KT_REQUIRE(katherine_transport_init_usb(&control, &messages) == KATHERINE_E_OK);
    KT_CHECK_EQ(fake_usb_opens(), 1);
    KT_CHECK_EQ(fake_usb_claims(), 1);

    katherine_usb_config_t stream = config_for(KATHERINE_USB_STREAM_ASYNC);
    KT_REQUIRE(katherine_usb_borrow_device(&stream, &control) == KATHERINE_E_OK);

    katherine_transport_t data;
    KT_REQUIRE(katherine_transport_init_usb(&data, &stream) == KATHERINE_E_OK);

    /* Nothing opened and nothing claimed a second time, and the ring is up. */
    KT_CHECK_EQ(fake_usb_opens(), 1);
    KT_CHECK_EQ(fake_usb_claims(), 1);
    KT_CHECK_EQ(fake_usb_submitted(), RING);

    /* The adopted half gives back its transfers and closes nothing. */
    katherine_transport_fini(&data);
    KT_CHECK_EQ(fake_usb_releases(), 0);
    KT_CHECK_EQ(fake_usb_closes(), 0);

    /* The half that opened closes, once. */
    katherine_transport_fini(&control);
    KT_CHECK_EQ(fake_usb_releases(), 1);
    KT_CHECK_EQ(fake_usb_closes(), 1);
}

/* The factory hands the whole address to the configuration, which reads the
 * scheme itself. Handing it only the remainder answers KATHERINE_E_ADDRESS,
 * the same code an empty bus gives, so a readout that is really there is what
 * tells the two apart. */
static void
test_the_control_factory_opens_a_usb_address(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t control;
    KT_REQUIRE(katherine_transport_init_for_control("usb://1-1", false, &control) == KATHERINE_E_OK);

    KT_CHECK_EQ(control.kind, KATHERINE_TRANSPORT_USB);
    KT_CHECK_EQ(fake_usb_claims(), 1);

    /* The message shape the control role asks for: no ring, nothing to lend. */
    KT_CHECK_EQ(fake_usb_submitted(), 0);
    KT_CHECK(control.ops->send_cmd != NULL);
    KT_CHECK(control.ops->acquire_data_buf == NULL);

    katherine_transport_fini(&control);
    KT_CHECK_EQ(fake_usb_releases(), 1);
}

/* And the data factory drives what the control one claimed. */
static void
test_the_data_factory_adopts_the_control_transport(void)
{
    fake_usb_reset();
    fake_usb_attach(0x16D0, 0x118A, 1, (const uint8_t[]) {1}, 1);

    katherine_transport_t control, data;
    KT_REQUIRE(katherine_transport_init_for_control("usb://1-1", false, &control) == KATHERINE_E_OK);
    KT_REQUIRE(katherine_transport_init_for_data("usb://1-1", &control, 0, &data) == KATHERINE_E_OK);

    /* One open and one claim between them, and the ring is up. */
    KT_CHECK_EQ(fake_usb_opens(), 1);
    KT_CHECK_EQ(fake_usb_claims(), 1);
    KT_CHECK(data.ops->acquire_data_buf != NULL);

    katherine_transport_fini(&data);
    KT_CHECK_EQ(fake_usb_releases(), 0);

    katherine_transport_fini(&control);
    KT_CHECK_EQ(fake_usb_releases(), 1);
}

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_opening_posts_the_whole_ring);
    KT_RUN(test_a_returned_buffer_is_posted_again);
    KT_RUN(test_an_empty_completion_does_not_shrink_the_ring);
    KT_RUN(test_a_short_timeout_still_carries_data);
    KT_RUN(test_silence_times_out);
    KT_RUN(test_returning_a_foreign_buffer_is_refused);
    KT_RUN(test_closing_waits_for_every_transfer);
    KT_RUN(test_a_failing_repost_is_reported);
    KT_RUN(test_the_synchronous_shape_reads_on_demand);
    KT_RUN(test_an_absent_device_is_reported);
    KT_RUN(test_a_ring_that_cannot_be_posted_unwinds);
    KT_RUN(test_opening_defines_every_field);
    KT_RUN(test_a_message_transport_lends_no_buffers);
    KT_RUN(test_a_stream_transport_sends_nothing);
    KT_RUN(test_a_second_independent_open_is_refused);
    KT_RUN(test_a_second_transport_adopts_the_first);
    KT_RUN(test_the_control_factory_opens_a_usb_address);
    KT_RUN(test_the_data_factory_adopts_the_control_transport);

    return kt_summary();
}
