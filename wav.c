/* record to file the I/Q stream(s) from a SDRplay RSP
 * RIFF/RF64/WAVE format
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *  
 * References:
 *  - EBU - TECH 3306: https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf
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
#define MAX_RIFF_SIZE 4294000000ULL

struct RIFFChunk {
    char chunkId[4];
    uint32_t chunkSize;
    char riffType[4];
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

struct DataChunk {
    char chunkId[4];
    uint32_t chunkSize;
};

struct RF64Chunk {
    char chunkId[4];
    uint32_t chunkSize;
    char rf64Type[4];
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
    uint32_t unused6[24];
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

typedef enum {
    WAV_TYPE_UNKNOWN,
    WAV_TYPE_RIFF,      /* 32 bit */
    WAV_TYPE_RF64,      /* 62 bit */
} WavType;

static WavType wav_type = WAV_TYPE_UNKNOWN;

/* internal functions */
static int write_riff_header(uint16_t block_alignment);
static int write_rf64_header(uint16_t block_alignment);
static int write_data_header();
static int finalize_riff_file(off_t data_chunk_offset, uint32_t riff_size);
static int finalize_rf64_file(unsigned long long riff_size);


int write_sdruno_header() {
    wav_type = estimate_data_size() < MAX_RIFF_SIZE ? WAV_TYPE_RIFF : WAV_TYPE_RF64;

    if (is_dual_tuner && frequency_A != frequency_B) {
        fprintf(stderr, "warning: SRuno auxi chunk can store only one center frequency\n");
    }

    uint16_t block_alignment = 2 * 2 * sizeof(short);
    if (wav_type == WAV_TYPE_RIFF) {
        if (write_riff_header(block_alignment) == -1) {
            return -1;
        }
    } else if (wav_type == WAV_TYPE_RF64) {
        if (write_rf64_header(block_alignment) == -1) {
            return -1;
        }
    }

    uint32_t gain_A = sdrplay_get_current_gain(0) * 1000 + 0.5;
    uint32_t gain_B = is_dual_tuner ? sdrplay_get_current_gain(1) * 1000 + 0.5 : 0;

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

    if (write(outputfd, &auxi_chunk, sizeof(auxi_chunk)) == -1) {
        return -1;
    }

    if (write_data_header() == -1) {
        return -1;
    }

    return 0;
}

int write_sdrconnect_header() {
    wav_type = estimate_data_size() < MAX_RIFF_SIZE ? WAV_TYPE_RIFF : WAV_TYPE_RF64;

    uint16_t block_alignment = 2 * sizeof(short);
    if (wav_type == WAV_TYPE_RIFF) {
        if (write_riff_header(block_alignment) == -1) {
            return -1;
        }
    } else if (wav_type == WAV_TYPE_RF64) {
        if (write_rf64_header(block_alignment) == -1) {
            return -1;
        }
    }

    if (write_data_header() == -1) {
        return -1;
    }

    return 0;
}

int write_experimental_header() {
    wav_type = estimate_data_size() < MAX_RIFF_SIZE ? WAV_TYPE_RIFF : WAV_TYPE_RF64;
    int max_num_markers = timeinfo.markers_max_idx;
    if (max_num_markers > 0) {
        wav_type = WAV_TYPE_RF64;
    }

    uint16_t block_alignment = 2 * 2 * sizeof(short);
    if (wav_type == WAV_TYPE_RIFF) {
        if (write_riff_header(block_alignment) == -1) {
            return -1;
        }
    } else if (wav_type == WAV_TYPE_RF64) {
        if (write_rf64_header(block_alignment) == -1) {
            return -1;
        }
    }

    if (max_num_markers > 0) {
        int chunk_size = max_num_markers * sizeof(struct MarkerEntry);
        struct MarkerChunk marker_chunk = {
            .chunkId = {'r', '6', '4', 'm'},
            .chunkSize = chunk_size
        };
        struct MarkerEntry empty_marker;
        memset(&empty_marker, 0, sizeof(empty_marker));

        if (write(outputfd, &marker_chunk, sizeof(marker_chunk)) == -1) {
            return -1;
        }
        for (int i = 0; i < max_num_markers; i++) {
            if (write(outputfd, &empty_marker, sizeof(empty_marker)) == -1) {
                return -1;
            }
        }
    }

    if (write_data_header() == -1) {
        return -1;
    }

    return 0;
}

int finalize_sdruno_file() {
    off_t data_chunk_offset = 0;
    if (wav_type == WAV_TYPE_RIFF) {
        data_chunk_offset = sizeof(struct RIFFChunk) +
                            sizeof(struct FormatChunk) +
                            sizeof(struct AuxiChunk);
        uint32_t riff_size = (uint32_t)(sizeof(char[4]) +
                                        sizeof(struct FormatChunk) +
                                        sizeof(struct AuxiChunk) +
                                        sizeof(struct DataChunk) +
                                        stats.data_size);
        if (finalize_riff_file(data_chunk_offset, riff_size) == -1) {
            return -1;
        }
    } else if (wav_type == WAV_TYPE_RF64) {
        unsigned long long riff_size = sizeof(char[4]) +
                                       sizeof(struct DataSize64Chunk) +
                                       sizeof(struct FormatChunk) +
                                       sizeof(struct AuxiChunk) +
                                       sizeof(struct DataChunk) +
                                       stats.data_size;
        if (finalize_rf64_file(riff_size) == -1) {
            return -1;
        }
    }

    // set startTime and stopTime in auxi chunk
    off_t auxi_chunk_offset = data_chunk_offset - sizeof(struct AuxiChunk);
    if (lseek(outputfd, auxi_chunk_offset + sizeof(char[4]) + sizeof(uint32_t), SEEK_SET) == -1) {
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

int finalize_sdrconnect_file() {
    off_t data_chunk_offset = 0;
    if (wav_type == WAV_TYPE_RIFF) {
        data_chunk_offset = sizeof(struct RIFFChunk) +
                            sizeof(struct FormatChunk);
        // it's magic - SDRconnect RIFF is always 36 bytes less than data size
        uint32_t riff_size = (uint32_t)(stats.data_size - 36);
        if (finalize_riff_file(data_chunk_offset, riff_size) == -1) {
            return -1;
        }
    } else if (wav_type == WAV_TYPE_RF64) {
        // it's magic - SDRconnect RIFF is always 36 bytes more than data size
        unsigned long long riff_size = stats.data_size + 36;
        if (finalize_rf64_file(riff_size) == -1) {
            return -1;
        }
    }

    return 0;
}

int finalize_experimental_file() {
    int max_num_markers = timeinfo.markers_max_idx;
    off_t markers_size = 0;
    if (max_num_markers > 0) {
        markers_size = sizeof(struct MarkerChunk) +
                       max_num_markers * sizeof(struct MarkerEntry);
    }

    off_t data_chunk_offset = 0;
    if (wav_type == WAV_TYPE_RIFF) {
        data_chunk_offset = sizeof(struct RIFFChunk) +
                            sizeof(struct FormatChunk) +
                            markers_size;
        uint32_t riff_size = (uint32_t)(sizeof(char[4]) +
                                        sizeof(struct FormatChunk) +
                                        sizeof(struct DataChunk) +
                                        stats.data_size);
        if (finalize_riff_file(data_chunk_offset, riff_size) == -1) {
            return -1;
        }
    } else if (wav_type == WAV_TYPE_RF64) {
        unsigned long long riff_size = sizeof(char[4]) +
                                       sizeof(struct DataSize64Chunk) +
                                       sizeof(struct FormatChunk) +
                                       markers_size +
                                       sizeof(struct DataChunk) +
                                       stats.data_size;
        if (finalize_rf64_file(riff_size) == -1) {
            return -1;
        }
    }

    // write time markers
    if (max_num_markers > 0) {
        off_t offset = data_chunk_offset - markers_size + sizeof(struct MarkerChunk);
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

/* internal functions */
static int write_riff_header(uint16_t block_alignment) {
    struct RIFFChunk riff_chunk = {
        .chunkId = {'R', 'I', 'F', 'F'},
        .chunkSize = 0,
        .riffType = {'W', 'A', 'V', 'E'}
    };

    uint16_t channelCount = is_dual_tuner ? 4 : 2;
    uint32_t bytesPerSecond = output_sample_rate * channelCount * sizeof(short);

    struct FormatChunk fmt_chunk = {
        .chunkId = {'f', 'm', 't', ' '},
        .chunkSize = sizeof(struct FormatChunk) - sizeof(char[4]) - sizeof(uint32_t),
        .formatType = WAVE_FORMAT_PCM,
        .channelCount = channelCount,
        .sampleRate = output_sample_rate,
        .bytesPerSecond = bytesPerSecond,
        .blockAlignment = block_alignment,
        .bitsPerSample = 16
    };

    if (write(outputfd, &riff_chunk, sizeof(riff_chunk)) == -1) {
        return -1;
    }
    if (write(outputfd, &fmt_chunk, sizeof(fmt_chunk)) == -1) {
        return -1;
    }

    return 0;
}

static int write_rf64_header(uint16_t block_alignment) {
    struct RF64Chunk rf64_chunk = {
        .chunkId = {'R', 'F', '6', '4'},
        .chunkSize = 0xffffffff,
        .rf64Type = {'W', 'A', 'V', 'E'}
    };

    struct DataSize64Chunk ds64_chunk = {
        .chunkId = {'d', 's', '6', '4'},
        .chunkSize = sizeof(struct DataSize64Chunk) - sizeof(char[4]) - sizeof(uint32_t),
    };

    uint16_t channelCount = is_dual_tuner ? 4 : 2;
    uint32_t bytesPerSecond = output_sample_rate * channelCount * sizeof(short);

    struct FormatChunk fmt_chunk = {
        .chunkId = {'f', 'm', 't', ' '},
        .chunkSize = sizeof(struct FormatChunk) - sizeof(char[4]) - sizeof(uint32_t),
        .formatType = WAVE_FORMAT_PCM,
        .channelCount = channelCount,
        .sampleRate = output_sample_rate,
        .bytesPerSecond = bytesPerSecond,
        .blockAlignment = block_alignment,
        .bitsPerSample = 16
    };

    if (write(outputfd, &rf64_chunk, sizeof(rf64_chunk)) == -1) {
        return -1;
    }
    if (write(outputfd, &ds64_chunk, sizeof(ds64_chunk)) == -1) {
        return -1;
    }
    if (write(outputfd, &fmt_chunk, sizeof(fmt_chunk)) == -1) {
        return -1;
    }

    return 0;
}

static int write_data_header() {
    uint32_t chunk_size = 0;
    if (wav_type == WAV_TYPE_RIFF) {
        chunk_size = 0;
    } else if (wav_type == WAV_TYPE_RF64) {
        chunk_size = 0xffffffff;
    }

    struct DataChunk data_chunk = {
        .chunkId = {'d', 'a', 't', 'a'},
        .chunkSize = chunk_size
    };

    if (write(outputfd, &data_chunk, sizeof(data_chunk)) == -1) {
        return -1;
    }

    return 0;
}

static int finalize_riff_file(off_t data_chunk_offset, uint32_t riff_size) {
    // fix data chunk size
    if (lseek(outputfd, data_chunk_offset + sizeof(char[4]), SEEK_SET) == -1) {
        fprintf(stderr, "lseek(data chunk size) failed: %s\n", strerror(errno));
        return -1;
    }
    uint32_t data_size_int = (uint32_t)stats.data_size;
    if (write(outputfd, &data_size_int, sizeof(data_size_int)) == -1) {
        return -1;
    }

    // fix RIFF chunk size
    if (lseek(outputfd, sizeof(char[4]), SEEK_SET) == -1) {
        fprintf(stderr, "lseek(RIFF chunk size) failed: %s\n", strerror(errno));
        return -1;
    }
    if (write(outputfd, &riff_size, sizeof(riff_size)) == -1) {
        return -1;
    }

    return 0;
}

static int finalize_rf64_file(unsigned long long riff_size) {
    // insert the RIFF size, 'data' chunk size and sample count in the 'ds64' chunk
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

    off_t offset = sizeof(struct RF64Chunk);
    if (lseek(outputfd, offset, SEEK_SET) == -1) {
        fprintf(stderr, "lseek(ds64 chunk) failed: %s\n", strerror(errno));
        return -1;
    }
    if (write(outputfd, &ds64_chunk, sizeof(ds64_chunk)) == -1) {
        return -1;
    }

    return 0;
}
