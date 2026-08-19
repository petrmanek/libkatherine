#!/usr/bin/env python3
"""Plot an integrated ToT hitmap from a pixel readout dump.

The input file is expected to contain whitespace-separated pixel hits
with columns X, Y, ToA, fToA and ToT (as printed e.g. by the krun
example). Any surrounding non-hit lines (chip id, frame markers,
statistics) are ignored. The ToT of every hit is summed into a 256x256
matrix indexed by the Cartesian (X, Y) coordinates and shown with
matplotlib's imshow(), origin at the bottom-left, zero-valued cells
transparent.
"""

import argparse

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

SENSOR_DIM = 256
COLUMNS = ['X', 'Y', 'ToA', 'fToA', 'ToT']


def parse_hits(path):
    # Read every line as up to 5 whitespace-separated fields, then keep
    # only fully numeric rows. This drops the preamble, the header row
    # and the trailing statistics without relying on fixed line numbers.
    df = pd.read_csv(path, sep=r'\s+', header=None, names=COLUMNS,
                     on_bad_lines='skip')
    df = df.apply(pd.to_numeric, errors='coerce').dropna().astype(int)

    in_range = (df['X'].between(0, SENSOR_DIM - 1)
                & df['Y'].between(0, SENSOR_DIM - 1))
    return df[in_range].reset_index(drop=True)


def integrate_tot(df):
    matrix = np.zeros((SENSOR_DIM, SENSOR_DIM))
    # Row index is Y, column index is X, so that imshow() maps X to the
    # horizontal axis and Y to the vertical one. np.add.at() accumulates
    # correctly even when the same pixel appears multiple times.
    np.add.at(matrix, (df['Y'].to_numpy(), df['X'].to_numpy()),
              df['ToT'].to_numpy())
    return matrix


def plot_hitmap(matrix, output=None):
    display = matrix.copy()
    display[display == 0] = np.nan  # zero cells become transparent

    fig, ax = plt.subplots()
    im = ax.imshow(display, cmap='jet', origin='lower', interpolation='none')
    ax.set_xlabel('X (pixel)')
    ax.set_ylabel('Y (pixel)')
    ax.set_title('Integrated ToT')
    fig.colorbar(im, ax=ax, label='ToT sum')

    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches='tight')
        print('Saved plot to %s' % output)
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('file', help='pixel dump file (e.g. stdout.log)')
    parser.add_argument('-o', '--output',
                        help='save the plot to this file instead of '
                             'showing it interactively')
    args = parser.parse_args()

    df = parse_hits(args.file)
    print('Parsed %d hits.' % len(df))

    matrix = integrate_tot(df)
    plot_hitmap(matrix, args.output)


if __name__ == '__main__':
    main()
