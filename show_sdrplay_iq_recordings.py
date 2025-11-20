#!/usr/bin/env python3
# show metadata stored in SDRuno/SDRconnect I/Q recordings
#
# Copyright 2024-2025 Franco Venturi
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# References:
# - https://medium.com/@jatin.dhall7385/pythonic-wav-file-handling-a-guide-to-reading-wav-files-without-external-libraries-f5869b27b2e7

from datetime import datetime, timezone
import struct
import sys

def fmt_chunk(fmt_bytes):
    fmt_code, number_of_channels, sample_rate, byte_rate, block_align, sample_width = struct.unpack('<2H2I2H', fmt_bytes)
    print('Format Code :', fmt_code)
    print('Number of Channels :', number_of_channels)
    print('Sample Rate :', sample_rate)
    print('Byte Rate :', byte_rate)
    print('Block align :', block_align)
    print('Sample Width :', sample_width)

def auxi_sdrplay_chunk(auxi_bytes):
    auxi = struct.unpack('<8H8H9I96s', auxi_bytes)
    print('Auxi :', auxi)
    start_time = datetime(auxi[0], auxi[1], auxi[3], auxi[4], auxi[5], auxi[6], auxi[7] * 1000, timezone.utc)
    print('Start Time :', start_time)
    stop_time = datetime(auxi[8], auxi[9], auxi[11], auxi[12], auxi[13], auxi[14], auxi[15] * 1000, timezone.utc)
    print('Stop Time :', stop_time)
    center_freq = auxi[16]
    print('Center Freq :', center_freq)
    ad_frequency = auxi[17]
    print('AD Frequency :', ad_frequency)
    if_frequency = auxi[18]
    print('IF Frequency :', if_frequency)
    bandwidth = auxi[19]
    print('Bandwidth :', bandwidth)
    iq_offset = auxi[20]
    print('IQ Offset :', iq_offset)
    db_offset = auxi[21]
    print('DB Offset :', db_offset, hex(db_offset))
    max_val = auxi[22]
    print('Max Val :', max_val)
    unused4 = auxi[23]
    print('Unused4 :', unused4)
    unused5 = auxi[24]
    print('Unused5 :', unused5)
    next_file = auxi[25].strip(b'\x00')
    print('Next File :', next_file)

def auxi_franco_chunk(auxi_bytes):
    auxi = struct.unpack('<8H8H9I', auxi_bytes)
    print('Auxi :', auxi)
    start_time = datetime(auxi[0], auxi[1], auxi[3], auxi[4], auxi[5], auxi[6], auxi[7] * 1000, timezone.utc)
    print('Start Time :', start_time)
    stop_time = datetime(auxi[8], auxi[9], auxi[11], auxi[12], auxi[13], auxi[14], auxi[15] * 1000, timezone.utc)
    print('Stop Time :', stop_time)
    center_freq = auxi[16]
    print('Center Freq :', center_freq)
    ad_frequency = auxi[17]
    print('AD Frequency :', ad_frequency)
    if_frequency = auxi[18]
    print('IF Frequency :', if_frequency)
    bandwidth = auxi[19]
    print('Bandwidth :', bandwidth)
    iq_offset = auxi[20]
    print('IQ Offset :', iq_offset)
    db_offset = auxi[21]
    print('DB Offset :', db_offset, hex(db_offset))
    max_val = auxi[22]
    print('Max Val :', max_val)
    unused4 = auxi[23]
    print('Unused4 :', unused4)
    unused5 = auxi[24]
    print('Unused5 :', unused5)

def ds64_chunk(ds64_bytes):
    riffSizeLow, riffSizeHigh, dataSizeLow, dataSizeHigh, sampleCountLow, sampleCountHigh, tableLength = struct.unpack('<7I', ds64_bytes)
    print('riffSizeLow :', riffSizeLow)
    print('riffSizeHigh :', riffSizeHigh)
    print('dataSizeLow :', dataSizeLow)
    print('dataSizeHigh :', dataSizeHigh)
    print('sampleCountLow :', sampleCountLow)
    print('sampleCountHigh :', sampleCountHigh)
    print('tableLength :', tableLength)
    riff_size = riffSizeHigh << 32 | riffSizeLow
    print('RIFF Size :', riff_size)
    data_size = dataSizeHigh << 32 | dataSizeLow
    print('Data Size :', data_size)
    sample_count = sampleCountHigh << 32 | sampleCountLow
    print('Sample Count :', sample_count)

def main():
    filename = sys.argv[1]
    with open(filename, 'rb') as wav:
        hdrchunk_bytes = wav.read(12)
        hdrchunk_id, hdrchunk_size, hdrchunk_fmt = struct.unpack('<4sI4s', hdrchunk_bytes)
        print('Header Chunk ID :', hdrchunk_id)
        print('Header Chunk Size :', hdrchunk_size if hdrchunk_size != 0xFFFFFFFF else -1)
        print('Header Chunk Format :', hdrchunk_fmt)
        print()
        # sanity check
        if not (hdrchunk_id in [b'RIFF', b'RF64'] and hdrchunk_fmt == b'WAVE'):
            raise ValueError("Invalid WAV file")

        while True:
            # read the next chunk header
            chunk_bytes = wav.read(8)
            if not chunk_bytes:
                break
            chunk_id, chunk_size = struct.unpack('<4sI', chunk_bytes)
            if not chunk_id.isascii():
                print('found garbage')
                break
            print('Chunk ID :', chunk_id)
            print('Chunk Size :', chunk_size if chunk_size != 0xFFFFFFFF else -1)
            if chunk_id == b'data':
                #wav.seek(chunk_size)
                break
            else:
                chunk_bytes = wav.read(chunk_size)
            if chunk_id == b'fmt ':
                fmt_chunk(chunk_bytes)
            elif chunk_id == b'auxi':
                if chunk_size == 164:
                    auxi_sdrplay_chunk(chunk_bytes)
                if chunk_size == 68:
                    auxi_franco_chunk(chunk_bytes)
            elif chunk_id == b'ds64':
                ds64_chunk(chunk_bytes)
            elif chunk_id == b'JUNK':
                # nothing interesting here
                pass
            print()


if __name__ == '__main__':
    main()
