#!/usr/bin/env python3
# show RSP I/Q recordings
#
# Copyright 2024-2025 Franco Venturi
#
# SPDX-License-Identifier: GPL-3.0-or-later

import struct
import sys

def main():
    channels = 1
    if len(sys.argv) > 1:
        channels = int(sys.argv[1])
    if channels == 1:
        count = 0
        imin = 32767
        imax = -32768
        qmin = 32767
        qmax = -32768
        while True:
           x = sys.stdin.buffer.read(4)
           if len(x) < 4:
               break
           count += 1
           i, q = struct.unpack('=2h', x)
           imin = min(imin, i)
           imax = max(imax, i)
           qmin = min(qmin, q)
           qmax = max(qmax, q)

        print('count:', count, file=sys.stderr)
        print('I range:', imin, imax, file=sys.stderr)
        print('Q range:', qmin, qmax, file=sys.stderr)

    elif channels == 2:
        count = 0
        iminA = 32767
        imaxA = -32768
        qminA = 32767
        qmaxA = -32768
        iminB = 32767
        imaxB = -32768
        qminB = 32767
        qmaxB = -32768
        while True:
           x = sys.stdin.buffer.read(8)
           if len(x) < 8:
               break
           count += 1
           iA, qA, iB, qB = struct.unpack('=4h', x)
           iminA = min(iminA, iA)
           imaxA = max(imaxA, iA)
           qminA = min(qminA, qA)
           qmaxA = max(qmaxA, qA)
           iminB = min(iminB, iB)
           imaxB = max(imaxB, iB)
           qminB = min(qminB, qB)
           qmaxB = max(qmaxB, qB)

        print('count:', count, file=sys.stderr)
        print('IA range:', iminA, imaxA, file=sys.stderr)
        print('QA range:', qminA, qmaxA, file=sys.stderr)
        print('IB range:', iminB, imaxB, file=sys.stderr)
        print('QB range:', qminB, qmaxB, file=sys.stderr)

    else:
        print('invalid number of channels:', channels, file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
