/**
 * @file
 * @brief Internal portable long-option command-line parser for ksim.
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// A minimal, portable substitute for getopt_long() -- ksim wants the same
// "--long-option [value]" surface on every platform, including ones with no
// getopt_long() at all (MSVC), without pulling in a full getopt shim.
//
// Short options are always a single dash followed by exactly one character;
// there is no bundling (e.g. "-qh") and no attached value (e.g. "-xVALUE").
// A short option's value, like a long option's, is either "=value" (long
// only) or the next, separate argv element.

// One entry of the option table passed to ksim_args_next(). The table is
// terminated by an entry with name == NULL.
typedef struct ksim_opt {
    const char *name; /* long name without the leading dashes, e.g. "listen" */
    char short_name;  /* optional single-dash short form, '\0' if none      */
    bool has_arg;     /* true = requires a value                            */
    int id;           /* returned by ksim_args_next()                       */
} ksim_opt_t;

// Parser state. There is no separate init function: set index = 1 (argv[0]
// is the program name and is never itself matched as an option).
typedef struct ksim_args {
    int index; /* next argv element to consume; initialize to 1 */
} ksim_args_t;

// Returns the basename portion of argv0 (after the last '/' or '\\'), so
// diagnostics name the program the way it was invoked, not the full path it
// was found at. Both separators are recognized unconditionally (rather than
// switched on KATHERINE_WIN) because argv[0] can carry a foreign-looking
// path regardless of the host platform (e.g. a Windows-style path recorded
// in a replayed command line), and recognizing '/' costs nothing on Windows.
static inline const char *
ksim_args_progname(const char *argv0)
{
    const char *slash     = strrchr(argv0, '/');
    const char *backslash = strrchr(argv0, '\\');
    const char *base      = argv0;

    if (slash != NULL && slash + 1 > base) base = slash + 1;
    if (backslash != NULL && backslash + 1 > base) base = backslash + 1;

    return base;
}

// Finds the option whose long name matches the name_len bytes at name, or
// NULL if there is none.
static inline const ksim_opt_t *
ksim_args_find_long(const ksim_opt_t *opts, const char *name, size_t name_len)
{
    for (const ksim_opt_t *o = opts; o->name != NULL; ++o) {
        if (strlen(o->name) == name_len && memcmp(o->name, name, name_len) == 0) return o;
    }
    return NULL;
}

// Finds the option whose short form is c, or NULL if there is none.
static inline const ksim_opt_t *
ksim_args_find_short(const ksim_opt_t *opts, char c)
{
    for (const ksim_opt_t *o = opts; o->name != NULL; ++o) {
        if (o->short_name != '\0' && o->short_name == c) return o;
    }
    return NULL;
}

/* Returns the matched option's id, -1 when argv is exhausted, or '?' after
 * printing a diagnostic to stderr (unknown option, missing value, or a
 * non-option positional argument). *value receives the option's value for
 * has_arg options (supports both "--name value" and "--name=value"; also
 * "-x value" for short forms), NULL otherwise.
 *
 * The bare token "--" ends option processing (as with getopt): it is
 * consumed and -1 is returned, leaving args->index at the first remaining
 * argv element. */
static inline int
ksim_args_next(ksim_args_t *args, int argc, char *const argv[], const ksim_opt_t *opts, const char **value)
{
    *value = NULL;

    if (args->index >= argc) return -1;

    const char *tok  = argv[args->index];
    const char *prog = ksim_args_progname(argv[0]);

    if (tok[0] != '-' || tok[1] == '\0') {
        // Neither "-..." nor a bare "-": a positional argument, which ksim's
        // command line never expects.
        fprintf(stderr, "%s: unexpected argument '%s'\n", prog, tok);
        ++args->index;
        return '?';
    }

    if (tok[1] == '-' && tok[2] == '\0') {
        // "--": end-of-options marker.
        ++args->index;
        return -1;
    }

    if (tok[1] == '-') {
        // Long option: "--name" or "--name=value".
        const char *name      = tok + 2;
        const char *eq        = strchr(name, '=');
        size_t name_len       = (eq != NULL) ? (size_t) (eq - name) : strlen(name);
        const ksim_opt_t *opt = ksim_args_find_long(opts, name, name_len);

        if (opt == NULL) {
            fprintf(stderr, "%s: unknown option '%s'\n", prog, tok);
            ++args->index;
            return '?';
        }

        if (opt->has_arg) {
            if (eq != NULL) {
                *value = eq + 1;
                ++args->index;
            } else {
                if (args->index + 1 >= argc) {
                    fprintf(stderr, "%s: option '--%s' requires a value\n", prog, opt->name);
                    ++args->index;
                    return '?';
                }
                *value = argv[args->index + 1];
                args->index += 2;
            }
        } else {
            if (eq != NULL) {
                fprintf(stderr, "%s: option '--%s' does not take a value\n", prog, opt->name);
                ++args->index;
                return '?';
            }
            ++args->index;
        }

        return opt->id;
    }

    // Short option: "-x", optionally followed by a separate value.
    if (tok[2] != '\0') {
        fprintf(stderr, "%s: unknown option '%s'\n", prog, tok);
        ++args->index;
        return '?';
    }

    const ksim_opt_t *opt = ksim_args_find_short(opts, tok[1]);
    if (opt == NULL) {
        fprintf(stderr, "%s: unknown option '%s'\n", prog, tok);
        ++args->index;
        return '?';
    }

    if (opt->has_arg) {
        if (args->index + 1 >= argc) {
            fprintf(stderr, "%s: option '-%c' requires a value\n", prog, tok[1]);
            ++args->index;
            return '?';
        }
        *value = argv[args->index + 1];
        args->index += 2;
    } else {
        ++args->index;
    }

    return opt->id;
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
