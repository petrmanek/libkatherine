/**
 * \file
 * \brief The one argument every example answers the same way.
 * \author Petr Mánek
 * \date 27.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <string.h>

/**
 * Whether the command line asks for the usage text.
 *
 * Scanned across every argument rather than tested at argv[1]: an example
 * whose first argument is an address is still asked for help by
 * `ktemp --chip --help`, and a reader who types it there means it.
 *
 * \param argc Argument count, as main() received it.
 * \param argv Argument vector, as main() received it.
 * \return True if any argument is -h or --help.
 */
static inline bool
kargs_wants_help(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) return true;
    }

    return false;
}
