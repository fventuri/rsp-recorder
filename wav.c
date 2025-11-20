/* record to file the I/Q stream(s) from a SDRplay RSP
 * RIFF/RF64/WAVE format
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *  
 * References:
 *  - EBU - TECH 3306: https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf
 *  - auxi chunk: SpectraVue User Guide: ver. 3.18 - https://www.moetronix.com/files/spectravue.pdf 
 */

#include "buffers.h"
#include "config.h"
#include "output.h"
#include "sdrplay-rsp.h"
#include "stats.h"
#include "wav.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WAVE_FORMAT_PCM 1

struct RIFFChunk {
    char chunkId[4];
    uint32_t chunkSize;
    char riffType[4];
};

struct RF64Chunk {
    char chunkId[4];
    uint32_t chunkSize;
    char rf64Type[4];
};

struct JunkChunk {
    char chunkId[4];
    uint32_t chunkSize;
    char chunkData[28];
};

struct DataSize64Chunk {
    char chunkId[4];
    uint32_t chunkSize;
    uint32_t riffSizeLow;
    uint32_t riffSizeHigh;
    uint32_t dataSizeLow;
    uint32_t dataSizeHigh;
    uint32_t sampleCountLow;
    uint32_t sampleCountHigh;
    uint32_t tableLength;
};

struct FormatChunk {
    char chunkId[4];
    uint32_t chunkSize;
    uint16_t formatType;
    uint16_t channelCount;
    uint32_t sampleRate;
    uint32_t bytesPerSecond;
    uint16_t blockAlignment;
    uint16_t bitsPerSample;
};

struct SystemTime {
    uint16_t year;
    uint16_t month;
    uint16_t dayOfWeek;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint16_t milliseconds;
};

struct AuxiChunk {
    char chunkId[4];
    uint32_t chunkSize;
    struct SystemTime startTime;
    struct SystemTime stopTime;
    uint32_t centerFreq;
    uint32_t adFrequency;
    uint32_t ifFrequency;
    uint32_t bandwidth;
    uint32_t iqOffset;
    uint32_t dbOffset;
    uint32_t maxVal;
    uint32_t unused4;
    uint32_t unused5;
};

struct Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint32_t data4;
    uint32_t data5;
};

struct MarkerEntry {
    uint32_t flags;
    uint32_t sampleOffsetLow;
    uint32_t sampleOffsetHigh;
    uint32_t byteOffsetLow;
    uint32_t byteOffsetHigh;
    uint32_t intraSmplOffsetHigh;
    uint32_t intraSmplOffsetLow;
    char labelText[256];
    uint32_t lablChunkIdentifier;
    struct Guid vendorAndProduct;
    uint32_t userData1;
    uint32_t userData2;
    uint32_t userData3;
    uint32_t userData4;
};

struct MarkerChunk {
    char chunkId[4];
    uint32_t chunkSize;
};

struct DataChunk {
    char chunkId[4];
    uint32_t chunkSize;
};

/* internal functions */
static int finalize_riff_file();
static int finalize_rf64_file();


int write_wav_header() {
    if (is_dual_tuner && frequency_A != frequency_B) {
        fprintf(stderr, "warning: WAV auxi chunk can store only one center frequency\n");
    }

    struct RIFFChunk riff_chunk = {
        .chunkId = {'R', 'I', 'F', 'F'},
        .chunkSize = 0,
        .riffType = {'W', 'A', 'V', 'E'}
    };

    struct JunkChunk junk_chunk = {
        .chunkId = {'J', 'U', 'N', 'K'},
        .chunkSize = sizeof(struct JunkChunk) - sizeof(char[4]) - sizeof(uint32_t)
    };

    uint16_t channelCount = 2;
    uint32_t bytesPerSecond = output_sample_rate * 2 * sizeof(short);
    if (is_dual_tuner) {
        channelCount = 4;
        bytesPerSecond = output_sample_rate * 2 * 2 * sizeof(short);
    }

    struct FormatChunk fmt_chunk = {
        .chunkId = {'f', 'm', 't', ' '},
        .chunkSize = sizeof(struct FormatChunk) - sizeof(char[4]) - sizeof(uint32_t),
        .formatType = WAVE_FORMAT_PCM,
        .channelCount = channelCount,
        .sampleRate = output_sample_rate,
        .bytesPerSecond = bytesPerSecond,
        .blockAlignment = 2 * 2 * sizeof(short),
        .bitsPerSample = 16
    };

    uint32_t gain_A = sdrplay_get_current_gain(0) * 1000 + 0.5;
    uint32_t gain_B = 0;
    if (is_dual_tuner) {
        gain_B = sdrplay_get_current_gain(1) * 1000 + 0.5;
    }

    struct AuxiChunk auxi_chunk = {
        .chunkId = {'a', 'u', 'x',  'i'},
        .chunkSize = sizeof(struct AuxiChunk) - sizeof(char[4]) - sizeof(uint32_t),
        .startTime = {0, 0, 0, 0, 0, 0, 0, 0},   /* to be filled at the end */
        .stopTime = {0, 0, 0, 0, 0, 0, 0, 0},    /* to be filled at the end */
        .centerFreq = (uint32_t) frequency_A,
        .adFrequency = 0,
        .ifFrequency = 0,
        .bandwidth = 0,
        .iqOffset = 0,
        .dbOffset = 0xe49b72a9,    /* same value as in SDRuno */
        .maxVal = 0,
        .unused4 = gain_A,
        .unused5 = gain_B
    };

    struct DataChunk data_chunk = {
        .chunkId = {'d', 'a', 't', 'a'},
        .chunkSize = 0
    };

    if (write(outputfd, &riff_chunk, sizeof(riff_chunk)) == -1) {
        return -1;
    }
    if (write(outputfd, &junk_chunk, sizeof(junk_chunk)) == -1) {
        return -1;
    }
    if (write(outputfd, &fmt_chunk, sizeof(fmt_chunk)) == -1) {
        return -1;
    }
    if (write(outputfd, &auxi_chunk, sizeof(auxi_chunk)) == -1) {
        return -1;
    }

    int max_num_markers = timeinfo.markers_max_idx;
    if (max_num_markers > 0) {
        int chunk_size = max_num_markers * sizeof(struct MarkerEntry);
        struct MarkerChunk marker_chunk = {
            .chunkId = {'r', '6', '4', 'm'},
            .chunkSize = chunk_size
        };
        if (write(outputfd, &marker_chunk, sizeof(marker_chunk)) == -1) {
            return -1;
        }
        struct MarkerEntry empty_marker;
        memset(&empty_marker, 0, sizeof(empty_marker));
        for (int i = 0; i < max_num_markers; i++) {
            if (write(outputfd, &empty_marker, sizeof(empty_marker)) == -1) {
                return -1;
            }
        }
    }

    if (write(outputfd, &data_chunk, sizeof(data_chunk)) == -1) {
        return -1;
    }

    return 0;
}

int finalize_wav_file() {
    unsigned long long riff_size = sizeof(char[4]) +
                                   sizeof(struct JunkChunk) +
                                   sizeof(struct FormatChunk) +
                                   sizeof(struct AuxiChunk) +
                                   sizeof(struct DataChunk) +
                                   stats.data_size;
    int max_num_markers = timeinfo.markers_max_idx;
    if (riff_size < UINT_MAX && max_num_markers == 0) {
        return finalize_riff_file();
    } else {
        return finalize_rf64_file();
    }
}

static int finalize_riff_file() {
    // 1. fix data chunk size
    off_t offset = sizeof(struct RIFFChunk) +
                   sizeof(struct JunkChunk) +
                   sizeof(struct FormatChunk) +
                   sizeof(struct AuxiChunk) +
                   sizeof(char[4]);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(data chunk size) failed: %s\n", strerror(errno));
        return -1;
    }
    uint32_t data_size_int = (uint32_t)stats.data_size;
    if (write(outputfd, &data_size_int, sizeof(data_size_int)) == -1) {
        return -1;
    }

    // 2. fix RIFF chunk size
    offset = sizeof(char[4]);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(RIFF chunk size) failed: %s\n", strerror(errno));
        return -1;
    }
    uint32_t riff_size = (uint32_t)(sizeof(char[4]) +
                                    sizeof(struct JunkChunk) +
                                    sizeof(struct FormatChunk) +
                                    sizeof(struct AuxiChunk) +
                                    sizeof(struct DataChunk) +
                                    stats.data_size);
    if (write(outputfd, &riff_size, sizeof(riff_size)) == -1) {
        return -1;
    }

    // 3. set startTime and stopTime in auxi chunk
    offset = sizeof(struct RIFFChunk) +
             sizeof(struct JunkChunk) +
             sizeof(struct FormatChunk) +
             sizeof(char[4]) + sizeof(uint32_t);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(auxi chunk startTime) failed: %s\n", strerror(errno));
        return -1;
    }
    struct tm *startTime_tm = gmtime(&timeinfo.start_ts.tv_sec);
    struct SystemTime startTime = {
        .year = 1900 + startTime_tm->tm_year,
        .month = startTime_tm->tm_mon + 1,
        .dayOfWeek = startTime_tm->tm_wday,
        .day = startTime_tm->tm_mday,
        .hour = startTime_tm->tm_hour,
        .minute = startTime_tm->tm_min,
        .second = startTime_tm->tm_sec,
        .milliseconds = timeinfo.start_ts.tv_nsec * 1e-6 + 0.5
    };
    if (write(outputfd, &startTime, sizeof(startTime)) == -1) {
        return -1;
    }
    struct tm *stopTime_tm = gmtime(&timeinfo.stop_ts.tv_sec);
    struct SystemTime stopTime = {
        .year = 1900 + stopTime_tm->tm_year,
        .month = stopTime_tm->tm_mon + 1,
        .dayOfWeek = stopTime_tm->tm_wday,
        .day = stopTime_tm->tm_mday,
        .hour = stopTime_tm->tm_hour,
        .minute = stopTime_tm->tm_min,
        .second = stopTime_tm->tm_sec,
        .milliseconds = timeinfo.stop_ts.tv_nsec * 1e-6 + 0.5
    };
    if (write(outputfd, &stopTime, sizeof(stopTime)) == -1) {
        return -1;
    }

    return 0;
}

static int finalize_rf64_file() {
    int max_num_markers = timeinfo.markers_max_idx;
    unsigned long long markers_size = 0L;
    if (max_num_markers > 0) {
        markers_size = sizeof(struct MarkerChunk) +
                       max_num_markers * sizeof(struct MarkerEntry);
    }

    /* 1. Replace the chunkID ‘JUNK’ with ‘ds64’ chunk. */
    /* 2. Insert the RIFF size, 'data' chunk size and sample count in the 'ds64' chunk */
    unsigned long long riff_size = sizeof(char[4]) +
                                   sizeof(struct JunkChunk) +
                                   sizeof(struct FormatChunk) +
                                   sizeof(struct AuxiChunk) +
                                   markers_size +
                                   sizeof(struct DataChunk) +
                                   stats.data_size;
    struct DataSize64Chunk ds64_chunk = {
        .chunkId = {'d', 's', '6', '4'},
        .chunkSize = sizeof(struct DataSize64Chunk) - sizeof(char[4]) - sizeof(uint32_t),
        .riffSizeLow = riff_size & 0xffffffff,
        .riffSizeHigh = riff_size >> 32,
        .dataSizeLow = stats.data_size & 0xffffffff,
        .dataSizeHigh = stats.data_size >> 32,
        .sampleCountLow = stats.output_samples & 0xffffffff,
        .sampleCountHigh = stats.output_samples >> 32,
        .tableLength = 0
    };
    off_t offset = sizeof(struct RIFFChunk);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(ds64 chunk) failed: %s\n", strerror(errno));
        return -1;
    }
    if (write(outputfd, &ds64_chunk, sizeof(ds64_chunk)) == -1) {
        return -1;
    }

    /* 3. Set RIFF size, 'data' chunk size and sample count in the 32 bit fields to -1 = FFFFFFFF hex */
    uint32_t negative1 = 0xffffffff;
    offset = sizeof(char[4]);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(RIFF chunk size) failed: %s\n", strerror(errno));
        return -1;
    }
    if (write(outputfd, &negative1, sizeof(negative1)) == -1) {
        return -1;
    }
    offset = sizeof(struct RIFFChunk) +
             sizeof(struct JunkChunk) +
             sizeof(struct FormatChunk) +
             sizeof(struct AuxiChunk) +
             markers_size +
             sizeof(char[4]);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(data chunk size) failed: %s\n", strerror(errno));
        return -1;
    }
    if (write(outputfd, &negative1, sizeof(negative1)) == -1) {
        return -1;
    }

    /* 4. Replace the ID ‘RIFF’ with ‘RF64’ in the first four bytes of the file */
    char rf64[4] = {'R', 'F', '6', '4'};
    offset = 0L;
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(RF64 ID) failed: %s\n", strerror(errno));
        return -1;
    }
    if (write(outputfd, &rf64, sizeof(rf64)) == -1) {
        return -1;
    }

    // 5. set startTime and stopTime in auxi chunk
    offset = sizeof(struct RIFFChunk) +
             sizeof(struct JunkChunk) +
             sizeof(struct FormatChunk) +
             sizeof(char[4]) + sizeof(uint32_t);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(auxi chunk startTime) failed: %s\n", strerror(errno));
        return -1;
    }
    struct tm *startTime_tm = gmtime(&timeinfo.start_ts.tv_sec);
    struct SystemTime startTime = {
        .year = 1900 + startTime_tm->tm_year,
        .month = startTime_tm->tm_mon + 1,
        .dayOfWeek = startTime_tm->tm_wday,
        .day = startTime_tm->tm_mday,
        .hour = startTime_tm->tm_hour,
        .minute = startTime_tm->tm_min,
        .second = startTime_tm->tm_sec,
        .milliseconds = timeinfo.start_ts.tv_nsec * 1e-6 + 0.5
    };
    if (write(outputfd, &startTime, sizeof(startTime)) == -1) {
        return -1;
    }
    struct tm *stopTime_tm = gmtime(&timeinfo.stop_ts.tv_sec);
    struct SystemTime stopTime = {
        .year = 1900 + stopTime_tm->tm_year,
        .month = stopTime_tm->tm_mon + 1,
        .dayOfWeek = stopTime_tm->tm_wday,
        .day = stopTime_tm->tm_mday,
        .hour = stopTime_tm->tm_hour,
        .minute = stopTime_tm->tm_min,
        .second = stopTime_tm->tm_sec,
        .milliseconds = timeinfo.stop_ts.tv_nsec * 1e-6 + 0.5
    };
    if (write(outputfd, &stopTime, sizeof(stopTime)) == -1) {
        return -1;
    }

    /* write the markers */
    if (max_num_markers > 0) {
        offset = sizeof(struct RIFFChunk) +
                 sizeof(struct JunkChunk) +
                 sizeof(struct FormatChunk) +
                 sizeof(struct AuxiChunk) +
                 sizeof(struct MarkerChunk);
        if (lseek(outputfd, offset, SEEK_SET) == -1) {
            fprintf(stderr, "lseek(marker chunk entries) failed: %s\n", strerror(errno));
            return -1;
        }
        int num_markers = timeinfo.markers_curr_idx;
        for (int i = 0; i < num_markers; i++) {
            TimeMarker *marker = &timeinfo.markers[i];
            struct MarkerEntry marker_entry = {
                .flags = 0x1,
                .sampleOffsetLow = marker->sample_num & 0xffffffff,
                .sampleOffsetHigh = marker->sample_num >> 32,
                .byteOffsetLow = 0,
                .byteOffsetHigh = 0,
                .intraSmplOffsetHigh = 0,
                .intraSmplOffsetLow = 0,
                .labelText = {0},
                .lablChunkIdentifier = 0,
                .vendorAndProduct = {0, 0, 0, 0, 0},
                .userData1 = 0,
                .userData2 = 0,
                .userData3 = 0,
                .userData4 = 0
            };
            struct tm *tm = gmtime(&marker->ts.tv_sec);
            char buffer[20];
            /* build ISO8601/RFC3339 timestamp */
            strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H%M:%S", tm);
            snprintf(marker_entry.labelText, 256, "%s.%09luZ", buffer, marker->ts.tv_nsec);
            if (write(outputfd, &marker_entry, sizeof(marker_entry)) == -1) {
                return -1;
            }
        }
    }

    return 0;
}
