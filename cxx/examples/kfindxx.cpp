//
// Created by petr on 23.6.18.
//

#include <iostream>
#include <katherinexx/katherinexx.hpp>

#include "address_source.hpp"

int n_found = 0;
int n_attempted = 0;

void
test_failed(const std::string& address)
{
    /* Ignored */
}

void
test_succeeded(const std::string& address, const std::string& chip_id)
{
    ++n_found;
    std::cout << "Found device: " << address << ",\t chip id: " << chip_id << std::endl;
}

void
test_ip_address(const std::string& address)
{
    ++n_attempted;

    try {
        katherine::device device{address};
        
        std::string chip_id = device.chip_id();
        test_succeeded(address, chip_id);
    } catch (const katherine::error&) {
        test_failed(address);
    }
}

void
abort_parsing(char *argv[], const std::string& message)
{
    std::cout << argv[0] << ": error parsing address range, " << message << std::endl;
    std::abort();
}

void
parse_range(int& min, int& max, const std::string& range)
{
    std::size_t dash_pos = range.find('-');

    if (dash_pos == std::string::npos) {
        // Expecting single number.
        min = max = std::stoi(range);
    } else {
        // Expecting two numbers separated by a dash.
        min = std::stoi(range.substr(0, dash_pos));
        max = std::stoi(range.substr(dash_pos + 1, range.length() - dash_pos - 1));
    }
}

void
parse_args(int argc, char *argv[], int min[4], int max[4])
{
    if (argc < 2) {
        return;
    }

    std::string arg{argv[1]};
    std::size_t pos[4];

    pos[0] = arg.find('.');
    if (pos[0] == std::string::npos) {
        abort_parsing(argv, "expected 3 dots (got 0)");
        return;
    }

    pos[1] = arg.find('.', pos[0] + 1);
    if (pos[1] == std::string::npos) {
        abort_parsing(argv, "expected 3 dots (got 1)");
        return;
    }

    pos[2] = arg.find('.', pos[1] + 1);
    if (pos[2] == std::string::npos) {
        abort_parsing(argv, "expected 3 dots (got 2)");
        return;
    }

    pos[3] = arg.size();

    int component_no = 1;
    try {
        parse_range(min[0], max[0], arg.substr(0, pos[0]));
        ++component_no;

        parse_range(min[1], max[1], arg.substr(pos[0] + 1, pos[1] - pos[0] - 1));
        ++component_no;

        parse_range(min[2], max[2], arg.substr(pos[1] + 1, pos[2] - pos[1] - 1));
        ++component_no;

        parse_range(min[3], max[3], arg.substr(pos[2] + 1, pos[3] - pos[2] - 1));
    } catch (const std::invalid_argument&) {
        abort_parsing(argv, "invalid range specified (component " + std::to_string(component_no) + ")");
    }
}

int
main(int argc, char *argv[])
{
    int min[] = {192, 168, 1, 100};
    int max[] = {192, 168, 1, 200};
    parse_args(argc, argv, min, max);

    address_source src{};
    src.set_bounds(min, max);

    int n_hosts = 1;
    for (int i = 0; i < 4; ++i) {
        n_hosts *= (max[i] - min[i] + 1);
    }

    std::cout << "Testing " << n_hosts << " hosts.\t Start: " << min[0] << '.' << min[1] << '.' << min[2] << '.' << min[3]
              << ",\t End: " << max[0] << '.' << max[1] << '.' << max[2] << '.' << max[3] << std::endl;

    while (src.have_address()) {
        test_ip_address(src.address());
        src.next_address();
    }

    std::cout << "Done. Tested " << n_attempted << " hosts (" << n_found << " found)." << std::endl;
    return n_found > 0 ? 0 : 1;
}
