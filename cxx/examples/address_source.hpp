/**
 * @file
 * @brief Helper class enumerating IP addresses in a given range.
 * @author Petr Mánek
 * @date 23.6.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

class address_source {
    bool have_address_;

    int min_[4];
    int max_[4];
    int cur_[4];

public:
    address_source();

    bool
    have_address() const;

    std::string
    address() const;

    void
    next_address();

    void
    set_bounds(int min[4], int max[4]);

};
