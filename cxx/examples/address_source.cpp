//
// Created by petr on 23.6.18.
//

#include <sstream>
#include "address_source.hpp"

bool
address_source::have_address() const
{
    return have_address_;
}

void
address_source::next_address()
{
    int i = 3;

    while (++cur_[i] > max_[i]) {
        if (i > 0) {
            cur_[i] = min_[i];
            --i;
        } else {
            have_address_ = false;
            break;
        }
    }
}

std::string
address_source::address() const
{
    std::stringstream ss{};
    ss << cur_[0] << '.' << cur_[1] << '.' << cur_[2] << '.' << cur_[3];
    return ss.str();
}

address_source::address_source()
    :have_address_{true}
{
}

void
address_source::set_bounds(int* min, int* max)
{
    for (int i = 0; i < 4; ++i) {
        cur_[i] = min_[i] = min[i];
        max_[i] = max[i];
    }

    have_address_ = true;
}
