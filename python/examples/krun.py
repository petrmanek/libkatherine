#!/usr/bin/env python3
# Example: configure and perform data-driven acquisition.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from timeit import default_timer as timer
import katherine as k

# Set the following flag to inject test pulses into a small subset of
# pixels during the acquisition.
TEST_PULSE = False


def paint_test_pixels(px_config):
    # Enable test pulses for pixels forming the letter 'P' (chosen because
    # it is asymmetric in both axes, making image orientation verifiable).
    shape = ['XXX.',
             'X..X',
             'XXX.',
             'X...',
             'X...']
    scale = 5          # each shape cell becomes a scale x scale block of pixels
    x0, y0 = 120, 120  # bottom-left corner of the shape
    n_rows = len(shape)
    n_painted = 0

    for row, line in enumerate(shape):
        for col, px in enumerate(line):
            if px != 'X':
                continue
            for dy in range(scale):
                for dx in range(scale):
                    # Rows of the shape are listed top-down while the Y axis
                    # grows upwards, hence the flipped row index.
                    x = x0 + scale * col + dx
                    y = y0 + scale * (n_rows - 1 - row) + dy

                    if not (0 <= x <= 255 and 0 <= y <= 255):
                        print('Test pulse pixel at X = %d,\t Y = %d is out of bounds, skipping.' % (x, y))
                        continue

                    px_config.set_test_bit(x, y, True)
                    n_painted += 1

    print('Test pulses enabled for %d pixels.' % n_painted)


def configure():
    c = k.Config()

    c.bias_id           = 0
    c.acq_time          = 10e9 # ns
    c.no_frames         = 1
    c.bias              = 230 # V

    c.delayed_start     = False

    c.start_trigger     = k.Trigger(enabled=False)
    c.stop_trigger      = k.Trigger(enabled=False)

    c.gray_disable      = False
    c.polarity_holes    = False

    c.phase             = k.Phase.PHASE_1
    c.freq              = k.Freq.FREQ_40

    dacs = k.Dacs()
    dacs.Ibias_Preamp_ON       = 128
    dacs.Ibias_Preamp_OFF      = 8
    dacs.VPReamp_NCAS          = 128
    dacs.Ibias_Ikrum           = 15
    dacs.Vfbk                  = 164
    dacs.Vthreshold_fine       = 476
    dacs.Vthreshold_coarse     = 8
    dacs.Ibias_DiscS1_ON       = 100
    dacs.Ibias_DiscS1_OFF      = 8
    dacs.Ibias_DiscS2_ON       = 128
    dacs.Ibias_DiscS2_OFF      = 8
    dacs.Ibias_PixelDAC        = 128
    dacs.Ibias_TPbufferIn      = 128
    dacs.Ibias_TPbufferOut     = 128
    dacs.VTP_coarse            = 128
    dacs.VTP_fine              = 256
    dacs.Ibias_CP_PLL          = 128
    dacs.PLL_Vcntrl            = 128

    px_config = k.PxConfig.from_bmc('chipconfig.bmc')

    # Test pulses are disabled unless requested above. (No explicit setup
    # is needed: a new Config keeps them off.)
    if TEST_PULSE:
        # Pulse the analog front-end with amplitude given by the difference
        # of the two DACs: |128 * 5 mV - 352 * 2.5 mV| = 240 mV.
        dacs.VTP_coarse = 128
        dacs.VTP_fine   = 352

        c.test_pulse_config = k.TestPulseConfig(
            enabled=True,
            count=100,
            period=6401)  # clock cycles, ~160 us @ 40 MHz

        paint_test_pixels(px_config)

    c.dacs = dacs
    c.pixel_config = px_config

    return c


class MyObserver(k.AcquisitionObserver):
    def __init__(self):
        self.n_hits = 0

    def frame_started(self, frame_idx):
        self.n_hits = 0
        
        print('Started frame %d.' % frame_idx)
        print('X\tY\tToA\tfToA\tToT')

    def frame_ended(self, frame_idx, completed, info):
        print('')
        print('Ended frame %d.' % frame_idx)
        print(' - tpx3->katherine lost %d pixels' % info.lost_pixels)
        print(' - katherine->pc sent %d pixels' % info.sent_pixels)
        print(' - katherine->pc received %d pixels' % info.received_pixels)
        print(' - state: %s' % ('completed' if completed else 'not completed'))
        print(' - start time: %d' % info.start_time)
        print(' - end time: %d' % info.end_time)

    def pixels_received(self, pixels):
        self.n_hits += len(pixels)

        for px in pixels:
            print('%d\t%d\t%d\t%d' % (px.x, px.y, px.timestamp, px.tot))


def print_chip_id(device):
    chip_id = device.get_chip_id()
    print('Chip ID: %s' % chip_id)


def run_acquisition(dev, c):
    acq = k.Acquisition(dev, k.MD_SIZE() * 34952533, k.PxFastToaTot.RAW_SIZE() * 4096, 500, 10000)
    acq.observer = MyObserver()

    print('Acquisition started')
    acq.begin(c, k.ReadoutType.DATA_DRIVEN, k.AcquisitionMode.TOA_TOT, True)

    tic = timer()
    acq.read()
    toc = timer()

    duration = toc - tic

    print('Acquisition completed:')
    print(' - state: %s' % acq.state)
    print(' - received %d complete frames' % acq.completed_frames)
    print(' - dropped %d measurement data' % acq.dropped_measurement_data)
    print(' - total hits: %d' % acq.observer.n_hits)
    print(' - total duration: %f s' % duration)
    print(' - throughput: %f hits/s' % (acq.observer.n_hits / duration))


if __name__ == '__main__':
    remote_addr = '192.168.1.145'

    config = configure()

    device = k.Device(remote_addr)
    print_chip_id(device)
    run_acquisition(device, config)
