#!/usr/bin/env python3
# show gains data
#
# Copyright 2025 Franco Venturi
#
# SPDX-License-Identifier: GPL-3.0-or-later

import struct
import sys

def main():
    filename = sys.argv[1]
    with open(filename, 'rb') as f:
        while True:
            gain_change = f.read(16)
            if not gain_change:
                break
            sample_num, currGain, tuner, gRdB, lnaGRdB = struct.unpack('@Qf3Bx', gain_change)
            print(f'sample_num={sample_num} currGain={currGain:.3f} tuner={tuner} gRdB={gRdB} lnaGRdB={lnaGRdB}')

if __name__ == '__main__':
    main()
