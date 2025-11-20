/* record to file the I/Q stream(s) from a SDRplay RSP
 * output functions
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"
#include "output.h"
#include "rsp-recorder.h"
#include "sdrplay-rsp.h"
#include "wav.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif


/* global variables */
int outputfd = -1;
int gainsfd = -1;
short *outsamples = NULL;

static bool is_output_open = false;
static bool is_outsamples_buffer_allocated = false;
static bool is_gains_open = false;

/* internal functions */
static int generate_output_filename(char *output_filename, int output_filename_max_size);
static int generate_gains_filename(const char *output_filename, char *gains_filename, int gains_filename_max_size);
static int write_linrad_header();


int output_open() {
    int errcode;

    char output_filename[PATH_MAX];
    errcode = generate_output_filename(output_filename, PATH_MAX);
    if (errcode != 0) {
        fprintf(stderr, "generate_output_filename(%s) failed\n", outfile_template);
        return -1;
    }

    if (strcmp(output_filename, "-") == 0) {
        if (!(output_type == OUTPUT_TYPE_RAW || output_type == OUTPUT_TYPE_LINRAD)) {
            fprintf(stderr, "stdout is only supported for raw and Linrad formats\n");
            return -1;
        }
        outputfd = fileno(stdout);
    } else {
        outputfd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    }
    if (outputfd == -1) {
        fprintf(stderr, "open(%s) for writing failed: %s\n", output_filename, strerror(errno));
        return -1;
    }
    is_output_open = true;

    if (output_type == OUTPUT_TYPE_LINRAD) {
        if (write_linrad_header() == -1) {
            fprintf(stderr, "write() Linrad header failed: %s\n", strerror(errno));
            return -1;
        }
    } else if (output_type == OUTPUT_TYPE_WAV) {
        if (write_wav_header() == -1) {
            fprintf(stderr, "write() WAV header failed: %s\n", strerror(errno));
            return -1;
        }
    }

    outsamples = (short *)malloc(samples_buffer_capacity * sizeof(short));   
    if (outsamples == NULL) {
        fprintf(stderr, "malloc(outsamples) failed\n");
        return -1;
    }
    is_outsamples_buffer_allocated = true; 

    if (gains_file_enable) {
        if (strrchr(output_filename, '.') == NULL) {
            fprintf(stderr, "gains file not supported when output file has no extension\n");
            return -1;
        }
        char gains_filename[PATH_MAX] = "";
        errcode = generate_gains_filename(output_filename, gains_filename, PATH_MAX);
        if (errcode != 0) {
            fprintf(stderr, "generate_gains_filename(%s) failed\n", outfile_template);
            return -1;
        }
        gainsfd = open(gains_filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
        if (gainsfd == -1) {
            fprintf(stderr, "open(%s) for writing failed: %s\n", gains_filename, strerror(errno));
            return -1;
        }
        is_gains_open = true;
    }
    return 0;
}

void output_close() {
    if (is_outsamples_buffer_allocated) {
        free(outsamples);
        outsamples = NULL;
        is_outsamples_buffer_allocated = false;
    }
    if (is_output_open) {
        if (output_type == OUTPUT_TYPE_WAV) {
            if (finalize_wav_file() == -1) {
                fprintf(stderr, "finalize() WAV file failed: %s\n", strerror(errno));
            }
        }
        close(outputfd);
        outputfd = -1;
        is_output_open = false;
    }
    if (is_gains_open) {
        close(gainsfd);
        gainsfd = -1;
        is_gains_open = false;
    }
}

static int generate_output_filename(char *output_filename, int output_filename_max_size) {
    const char freq_place_holder[] = "{FREQ}";
    int freq_place_holder_len = sizeof(freq_place_holder) - 1;
    const char freqhz_place_holder[] = "{FREQHZ}";
    int freqhz_place_holder_len = sizeof(freqhz_place_holder) - 1;
    const char freqkhz_place_holder[] = "{FREQKHZ}";
    int freqkhz_place_holder_len = sizeof(freqkhz_place_holder) - 1;
    const char timestamp_place_holder[] = "{TIMESTAMP}";
    int timestamp_place_holder_len = sizeof(timestamp_place_holder) - 1;
    const char tsiso8601_place_holder[] = "{TSISO8601}";
    int tsiso8601_place_holder_len = sizeof(tsiso8601_place_holder) - 1;

    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);

    const char *src = outfile_template;
    char *dst = output_filename;
    char *dstlast = dst + output_filename_max_size - 1;
    char *p;
    while ((p = strchr(src, '{')) != NULL) {
        size_t sz = p - src;
        if (dst + sz > dstlast)
            return -1;
        strncpy(dst, src, sz);
        src = p;
        dst += sz;
        if (strncmp(src, freq_place_holder, freq_place_holder_len) == 0) {
            sz = dstlast - dst;
            size_t nf;
            if (!is_dual_tuner || frequency_A == frequency_B) {
                nf = snprintf(dst, sz, "%.0lf", frequency_A);
            } else {
                nf = snprintf(dst, sz, "%.0lf-%.0lf", frequency_A, frequency_B);
            }
            if (nf >= sz)
                return -1;
            src += freq_place_holder_len;
            dst += nf;
        } else if (strncmp(src, freqhz_place_holder, freqhz_place_holder_len) == 0) {
            sz = dstlast - dst;
            size_t nf;
            if (!is_dual_tuner || frequency_A == frequency_B) {
                nf = snprintf(dst, sz, "%.0lfHz", frequency_A);
            } else {
                nf = snprintf(dst, sz, "%.0lfHz-%.0lfHz", frequency_A, frequency_B);
            }
            if (nf >= sz)
                return -1;
            src += freqhz_place_holder_len;
            dst += nf;
        } else if (strncmp(src, freqkhz_place_holder, freqkhz_place_holder_len) == 0) {
            sz = dstlast - dst;
            size_t nf;
            if (!is_dual_tuner || frequency_A == frequency_B) {
                nf = snprintf(dst, sz, "%.0lfkHz", frequency_A / 1e3);
            } else {
                nf = snprintf(dst, sz, "%.0lfkHz-%.0lfkHz", frequency_A / 1e3, frequency_B / 1e3);
            }
            if (nf >= sz)
                return -1;
            src += freqkhz_place_holder_len;
            dst += nf;
        } else if (strncmp(src, timestamp_place_holder, timestamp_place_holder_len) == 0) {
            sz = dstlast - dst;
            size_t nts = strftime(dst, sz, "%Y%m%d_%H%M%SZ", tm);
            if (nts == 0)
                return -1;
            src += timestamp_place_holder_len;
            dst += nts;
        } else if (strncmp(src, tsiso8601_place_holder, tsiso8601_place_holder_len) == 0) {
            sz = dstlast - dst;
            size_t nts = strftime(dst, sz, "%Y%m%dT%H%M%SZ", tm);
            if (nts == 0)
                return -1;
            src += tsiso8601_place_holder_len;
            dst += nts;
        } else {
            if (dst == dstlast)
                return -1;
            *dst = *src;
            src++;
            dst++;
        }
    }
    size_t sz = strlen(src);
    if (dst + sz > dstlast)
        return -1;
    memcpy(dst, src, sz + 1);
    /* terminate string with a null */
    *(dst + sz) = '\0';
    return 0;
}

static int generate_gains_filename(const char *output_filename, char *gains_filename, int gains_filename_max_size) {
    const char gains_extension[] = ".gains";
    char *p = strrchr(output_filename, '.');
    size_t sz = (size_t)(p - output_filename);
    if (sz + sizeof(gains_extension) > (size_t)gains_filename_max_size)
        return -1;
    memcpy(gains_filename, output_filename, sz);
    memcpy(gains_filename + sz, gains_extension, sizeof(gains_extension));
    return 0;
}

/* Linrad format */
#define LINRAD_REMEMBER_UNKNOWN -1
#define LINRAD_TWO_CHANNELS 2
#define LINRAD_IQ_DATA 4
#define LINRAD_DIGITAL_IQ 32

typedef struct __attribute__((packed)) {
    int remember_proprietary_chunk;
    double timestamp;
    double passband_center;
    int passband_direction;
    int rx_input_mode;
    int rx_rf_channels;
    int rx_ad_channels;
    int rx_ad_speed;
    unsigned char save_init_flag;
} LinradHeader;

static int write_linrad_header() {
    if (is_dual_tuner && frequency_A != frequency_B) {
        fprintf(stderr, "warning: Linrad header does not support different passband center frequencies for the two tuners\n");
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double timestamp = (double) ts.tv_sec + 1e-9 * ts.tv_nsec;

    int rx_input_mode = LINRAD_IQ_DATA | LINRAD_DIGITAL_IQ;
    int rx_rf_channels = 1;
    int rx_ad_channels = 2;
    if (is_dual_tuner) {
        rx_input_mode |= LINRAD_TWO_CHANNELS;
        rx_rf_channels = 2;
        rx_ad_channels = 4;
    }
    LinradHeader linrad_header = {
        .remember_proprietary_chunk = LINRAD_REMEMBER_UNKNOWN,
        .timestamp = timestamp,
        .passband_center = frequency_A / 1e6,
        .passband_direction = 1,
        .rx_input_mode = rx_input_mode,
        .rx_rf_channels = rx_rf_channels,
        .rx_ad_channels = rx_ad_channels,
        .rx_ad_speed = output_sample_rate,
        .save_init_flag = 0
    };
    if (write(outputfd, &linrad_header, sizeof(linrad_header)) == -1) {
        fprintf(stderr, "write linrad header failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}
