#!/usr/bin/env python3

from functools import reduce
from katherine import Device

def format_ip(a, b=None, c=None, d=None):        
    return '%d.%d.%d.%d' % (tuple(a) if isinstance(a, list) else (a, b, c, d)) 


def check_address(a0, a1, a2, a3):
    addr = format_ip(a0, a1, a2, a3)
    chip_id = None

    try:
        dev = Device(addr)
        chip_id = dev.get_chip_id()
    except:
        return False

    print('Found device: %s,\t chip id: %s' % (addr, chip_id))
    return True


def search_range():
    mins = [192, 168, 1, 100]
    maxs = [192, 168, 1, 200]

    n_hosts = reduce(lambda x, y: x*y, [maxs[i] - mins[i] + 1 for i in range(4)])
    print('Testing %d hosts.\t Start: %s,\t End: %s' % (n_hosts, format_ip(mins), format_ip(maxs)))

    n_attempted = 0
    n_found = 0

    for a0 in range(mins[0], maxs[0]+1):
        for a1 in range(mins[1], maxs[1]+1):
            for a2 in range(mins[2], maxs[2]+1):
                for a3 in range(mins[3], maxs[3]+1):
                    found = check_address(a0, a1, a2, a3)
                    n_attempted += 1
                    n_found += 1 if found else 0

    print('Done. Tested %d hosts (%d found).' % (n_attempted, n_found))


if __name__ == '__main__':
    search_range()
