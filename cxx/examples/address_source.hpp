//
// Created by petr on 23.6.18.
//

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
