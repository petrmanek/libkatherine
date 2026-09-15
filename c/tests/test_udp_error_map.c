/**
 * \file
 * \brief Both platforms' socket-error mappings, asserted against each other.
 *
 * The transports translate an OS error into katherine_error_t through a table
 * per platform, and the property that matters is not what either table says on
 * its own but that the two agree: the same wire failure must yield the same
 * code whether it was observed through Winsock or through errno, or the error
 * domain 2.0 is built around means something different depending on where the
 * program runs.
 *
 * That agreement used to be believed rather than checked, and it was false.
 * `udp_win.c` fed raw Winsock codes to a table written in errno values, whose
 * numbering is unrelated -- WSAEINVAL is 10022 where EINVAL is 22 -- so every
 * Windows code missed every case and fell through to the caller's fallback,
 * leaving KATHERINE_E_INVAL and KATHERINE_E_NOMEM unreachable on that platform
 * and KATHERINE_E_TIMEOUT reachable only because the receive path manufactured
 * an EAGAIN before the table ran.
 *
 * Both tables are therefore written out here as data, and two different things
 * are asserted about them. Each platform's rows are run through the live
 * mapper, which only its own build can do. But the *outcomes* the two tables
 * claim are compared on every platform, so a case added to one and forgotten
 * in the other fails the suite wherever it runs -- which is the half of this
 * that would have caught the original defect on Linux.
 *
 * \author Petr Mánek
 * \date 15.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <katherine/error.h>

#include "transport/udp_error_map.h"

#include "ktest.h"

/// One row of a platform's mapping: the OS code a syscall reports, and the
/// enumerator the transport must translate it into.
typedef struct {
    int code;
    katherine_error_t mapped;
    const char *name;
} map_row_t;

// The two tables are spelled differently on purpose, and the asymmetry is the
// whole lesson of the file.
//
// Winsock is one ABI. WSABASEERR is 10000 and every WSAE* is that plus a fixed
// offset, the same on every Windows there is, so those rows are literals --
// which is also what makes them readable in a build that has no winsock2.h.
// Measured on MSVC 19.51.
//
// errno is NOT one ABI. The values differ per C library, so those rows are
// built from the macros and are whatever the platform being compiled says.
// Writing them as literals is a mistake this test made and macOS caught:
// glibc's EAGAIN is 11 and its ETIMEDOUT 110, Darwin agrees on neither, and
// EINVAL and ENOMEM coincide just often enough to make the error look like a
// two-row typo. MSVC's own <errno.h> is a third numbering again -- ETIMEDOUT
// 138, EWOULDBLOCK 140 -- which is why it can never be fed to the Winsock
// table.
//
// clang-format off

/// Winsock codes the transport must recognize, and what each must become.
static const map_row_t WSA_ROWS[] = {
    {10060, KATHERINE_E_TIMEOUT, "WSAETIMEDOUT"},
    {10035, KATHERINE_E_TIMEOUT, "WSAEWOULDBLOCK"},
    {10022, KATHERINE_E_INVAL,   "WSAEINVAL"},
    {10055, KATHERINE_E_NOMEM,   "WSAENOBUFS"},
};

/// The errno codes, and what each must become. Read from the macros, so this
/// states which category each code belongs to without asserting any platform's
/// numbering -- the claim that survives being compiled anywhere.
///
/// EWOULDBLOCK is absent because the C libraries this runs on define it as
/// EAGAIN, making a row for it a duplicate of the first; the header's own table
/// spells that case with an #if for the library that does not.
static const map_row_t ERRNO_ROWS[] = {
    {EAGAIN,    KATHERINE_E_TIMEOUT, "EAGAIN"},
    {ETIMEDOUT, KATHERINE_E_TIMEOUT, "ETIMEDOUT"},
    {EINVAL,    KATHERINE_E_INVAL,   "EINVAL"},
    {ENOMEM,    KATHERINE_E_NOMEM,   "ENOMEM"},
};

// clang-format on

/// Codes neither table claims, which must therefore reach the fallback
/// untouched. The Winsock entries are deliberately chosen from the band the
/// mapper does read (10000-11999), so that a table matching too broadly is
/// caught rather than merely one matching too narrowly.
static const int UNMAPPED[] = {
    0,
    1,
    EINTR,
    10004 /* WSAEINTR */,
    10040 /* WSAEMSGSIZE */,
    10054 /* WSAECONNRESET */,
    32767,
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

#ifdef KATHERINE_WIN
#define LIVE_ROWS  WSA_ROWS
#define OTHER_ROWS ERRNO_ROWS
#else
#define LIVE_ROWS  ERRNO_ROWS
#define OTHER_ROWS WSA_ROWS
#endif

/// Every row of this platform's table maps as the table says.
static void
test_live_table_matches_its_rows(void)
{
    for (size_t i = 0; i < COUNT(LIVE_ROWS); ++i) {
        const map_row_t row = LIVE_ROWS[i];

        // The fallback is passed as a value no row claims, so a row that fails
        // to match is visible as the fallback rather than coinciding with the
        // answer by luck.
        KT_CHECK_EQ(katherine_udp_map_socket_error(row.code, KATHERINE_E_IO), row.mapped);
        KT_CHECK_EQ(katherine_udp_map_socket_error(row.code, KATHERINE_E_SYSTEM), row.mapped);

        if (katherine_udp_map_socket_error(row.code, KATHERINE_E_IO) != row.mapped) {
            printf("#   %s (%d) did not map as documented\n", row.name, row.code);
        }
    }
}

/// A code outside the table reaches the caller's fallback, whichever it is.
static void
test_unmapped_codes_reach_the_fallback(void)
{
    for (size_t i = 0; i < COUNT(UNMAPPED); ++i) {
        KT_CHECK_EQ(katherine_udp_map_socket_error(UNMAPPED[i], KATHERINE_E_IO), KATHERINE_E_IO);
        KT_CHECK_EQ(katherine_udp_map_socket_error(UNMAPPED[i], KATHERINE_E_SYSTEM), KATHERINE_E_SYSTEM);
    }
}

/// Returns whether rows claim an enumerator.
static bool
claims(const map_row_t *rows, size_t count, katherine_error_t code)
{
    for (size_t i = 0; i < count; ++i) {
        if (rows[i].mapped == code) return true;
    }

    return false;
}

/// Returns whether codes contains one.
static bool
listed(const katherine_error_t *codes, size_t count, katherine_error_t code)
{
    for (size_t i = 0; i < count; ++i) {
        if (codes[i] == code) return true;
    }

    return false;
}

/// The two tables reach the same set of enumerators.
///
/// This is the cross-platform half, and it runs in both builds: the tables are
/// data, so the comparison needs neither platform's headers. A code one
/// platform can produce and the other cannot is exactly the defect this file
/// was written for, and it fails here rather than waiting for the other
/// platform's CI leg.
static void
test_both_tables_reach_the_same_codes(void)
{
    static const katherine_error_t MAPPED_CODES[] = {
        KATHERINE_E_TIMEOUT,
        KATHERINE_E_INVAL,
        KATHERINE_E_NOMEM,
    };

    for (size_t i = 0; i < COUNT(MAPPED_CODES); ++i) {
        const katherine_error_t code = MAPPED_CODES[i];

        KT_CHECK(claims(WSA_ROWS, COUNT(WSA_ROWS), code));
        KT_CHECK(claims(ERRNO_ROWS, COUNT(ERRNO_ROWS), code));

        if (!claims(WSA_ROWS, COUNT(WSA_ROWS), code) || !claims(ERRNO_ROWS, COUNT(ERRNO_ROWS), code)) {
            printf("#   %s is not reachable on both platforms\n", katherine_strerror(code));
        }
    }

    // Neither table may reach anything outside that set: a new case has to be
    // added to this test, and therefore to both tables, rather than to one
    // transport alone.
    for (size_t i = 0; i < COUNT(LIVE_ROWS); ++i) {
        KT_CHECK(listed(MAPPED_CODES, COUNT(MAPPED_CODES), LIVE_ROWS[i].mapped));
    }
    for (size_t i = 0; i < COUNT(OTHER_ROWS); ++i) {
        KT_CHECK(listed(MAPPED_CODES, COUNT(MAPPED_CODES), OTHER_ROWS[i].mapped));
    }
}

/// The codes this file calls unmapped really are absent from both tables.
///
/// A guard on the test's own data rather than on the library. The errno rows
/// are read from macros whose values this build cannot know in advance, so a C
/// library numbering one of them the way UNMAPPED numbers something else would
/// otherwise turn the rows above into a contradiction and report it as a
/// mapping bug. Checked here so that it reports itself instead.
static void
test_the_unmapped_codes_are_really_unmapped(void)
{
    for (size_t i = 0; i < COUNT(UNMAPPED); ++i) {
        for (size_t j = 0; j < COUNT(LIVE_ROWS); ++j) {
            KT_CHECK(UNMAPPED[i] != LIVE_ROWS[j].code);

            if (UNMAPPED[i] == LIVE_ROWS[j].code) {
                printf("#   this platform numbers %s as %d, which UNMAPPED also claims\n", LIVE_ROWS[j].name,
                    LIVE_ROWS[j].code);
            }
        }
    }
}

/// The two domains do not overlap, which is why mixing them up was silent.
///
/// Asserted rather than remarked upon, because the assertion is what makes the
/// comment in udp_error_map.h checkable: if some future platform's errno value did
/// coincide with a Winsock code, feeding one table the other's input would
/// start producing a plausible wrong answer instead of an obvious fallback,
/// and the reasoning in that header would no longer hold.
static void
test_the_two_domains_are_disjoint(void)
{
    for (size_t i = 0; i < COUNT(WSA_ROWS); ++i) {
        for (size_t j = 0; j < COUNT(ERRNO_ROWS); ++j) {
            KT_CHECK(WSA_ROWS[i].code != ERRNO_ROWS[j].code);
        }
    }
}

int
main(void)
{
    KT_RUN(test_live_table_matches_its_rows);
    KT_RUN(test_unmapped_codes_reach_the_fallback);
    KT_RUN(test_both_tables_reach_the_same_codes);
    KT_RUN(test_the_unmapped_codes_are_really_unmapped);
    KT_RUN(test_the_two_domains_are_disjoint);
    return kt_summary();
}
