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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef WIN32
// _setmode
#include <io.h>
#endif /* WIN32 */

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
        if (!(output_type == OUTPUT_TYPE_WAVVIEWDX_RAW || output_type == OUTPUT_TYPE_LINRAD)) {
            fprintf(stderr, "stdout is only supported for WavViewDX-raw and Linrad formats\n");
            return -1;
        }
        outputfd = fileno(stdout);
#ifdef WIN32
        _setmode(outputfd, _O_BINARY);
#endif
    } else if (output_filename[0] == '|') {
        if (!(output_type == OUTPUT_TYPE_WAVVIEWDX_RAW || output_type == OUTPUT_TYPE_LINRAD)) {
            fprintf(stderr, "named pipe is only supported for WavViewDX-raw and Linrad formats\n");
            return -1;
        }
        int startidx = 1;
        int endidx = strlen(output_filename);
        while (startidx < endidx && isspace(output_filename[startidx]))
            startidx++;
        char * output_pipename = output_filename + startidx;
        if (strlen(output_pipename) == 0) {
            fprintf(stderr, "empty named pipe name\n");
            return -1;
        }
        outputfd = open(output_pipename, O_WRONLY | O_BINARY);
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
    } else if (output_type == OUTPUT_TYPE_SDRUNO) {
        if (write_sdruno_header() == -1) {
            fprintf(stderr, "write() SDRuno header failed: %s\n", strerror(errno));
            return -1;
        }
    } else if (output_type == OUTPUT_TYPE_SDRCONNECT) {
        if (write_sdrconnect_header() == -1) {
            fprintf(stderr, "write() SDRconnect header failed: %s\n", strerror(errno));
            return -1;
        }
    } else if (output_type == OUTPUT_TYPE_EXPERIMENTAL) {
        if (write_experimental_header() == -1) {
            fprintf(stderr, "write() experimental format header failed: %s\n", strerror(errno));
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
        if (output_type == OUTPUT_TYPE_SDRUNO) {
            if (finalize_sdruno_file() == -1) {
                fprintf(stderr, "finalize() SDRuno file failed: %s\n", strerror(errno));
            }
        } else if (output_type == OUTPUT_TYPE_SDRCONNECT) {
            if (finalize_sdrconnect_file() == -1) {
                fprintf(stderr, "finalize() SDRconnect file failed: %s\n", strerror(errno));
            }
        } else if (output_type == OUTPUT_TYPE_EXPERIMENTAL) {
            if (finalize_experimental_file() == -1) {
                fprintf(stderr, "finalize() experimental format file failed: %s\n", strerror(errno));
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

int output_validate_filename() {
    const char wavviewdx_raw_placeholder[] = "{WAVVIEWDX-RAW}";
    int wavviewdx_raw_placeholder_len = sizeof(wavviewdx_raw_placeholder) - 1;
    const char sdruno_placeholder[] = "{SDRUNO}";
    int sdruno_placeholder_len = sizeof(sdruno_placeholder) - 1;
    const char sdrconnect_placeholder[] = "{SDRCONNECT}";
    int sdrconnect_placeholder_len = sizeof(sdrconnect_placeholder) - 1;

    if (output_type == OUTPUT_TYPE_WAVVIEWDX_RAW) {
        if (strcmp(outfile_template, "-") == 0 || outfile_template[0] == '|') {
            return 0;
        }
        const char *ps = strstr(outfile_template, wavviewdx_raw_placeholder);
        if (ps == NULL) {
            fprintf(stderr, "output type WavViewDX-raw requires '{WAVVIEWDX-RAW}' in the output filename\n");
            return -1;
        }
        const char *pe = ps + wavviewdx_raw_placeholder_len;
        if (!((ps == outfile_template || *(ps-1) == '/' || *(ps-1) == '\\') &&
               (pe + 4 <= outfile_template + strlen(outfile_template)) &&
               (*pe == '.' || *pe == '_' ||
               (*pe == 'z' && (*(pe+1) == '.' || *(pe+1) == '_'))))) {
            fprintf(stderr, "for WavViewDX-raw files the string '{WAVVIEWDX-RAW}' must be at the beginning of the output filename and must be followed by '.', '_', or 'z'\n");
            return -1;
        }
        if (strstr(outfile_template, sdruno_placeholder) != NULL || strstr(outfile_template, sdrconnect_placeholder) != NULL) {
            fprintf(stderr, "output type WavViewDX-raw cannot have '{SDRUNO}' or '{SDRCONNECT}' in the output filename\n");
            return -1;
        }
    } else if (output_type == OUTPUT_TYPE_SDRUNO) {
        const char *ps = strstr(outfile_template, sdruno_placeholder);
        if (ps == NULL) {
            fprintf(stderr, "output type SDRuno requires '{SDRUNO}' in the output filename\n");
            return -1;
        }
        const char *pe = ps + sdruno_placeholder_len;
        if (!((ps == outfile_template || *(ps-1) == '/' || *(ps-1) == '\\') &&
               (pe + 4 <= outfile_template + strlen(outfile_template)) &&
               (*pe == '.' || *pe == '_'))) {
            fprintf(stderr, "for SDRuno files the string '{SDRUNO}' must be at the beginning of the output filename and must be followed by either '.' or '_'\n");
            return -1;
        }
        if (strstr(outfile_template, wavviewdx_raw_placeholder) != NULL || strstr(outfile_template, sdrconnect_placeholder) != NULL) {
            fprintf(stderr, "output type SDRuno cannot have '{WAVVIEWDX-RAW}' or '{SDRCONNECT}' in the output filename\n");
            return -1;
        }
    } else if (output_type == OUTPUT_TYPE_SDRCONNECT) {
        const char *ps = strstr(outfile_template, sdrconnect_placeholder);
        if (ps == NULL) {
            fprintf(stderr, "output type SDRconnect requires '{SDRCONNECT}' in the output filename\n");
            return -1;
        }
        const char *pe = ps + sdrconnect_placeholder_len;
        if (!((ps == outfile_template || *(ps-1) == '/' || *(ps-1) == '\\') &&
               (pe + 4 <= outfile_template + strlen(outfile_template)) &&
               (*pe == '.' || *pe == '_'))) {
            fprintf(stderr, "for SDRconnect files the string '{SDRCONNECT}' must be at the beginning of the output filename and must be followed by either '.' or '_'\n");
            return -1;
        }
        if (strstr(outfile_template, wavviewdx_raw_placeholder) != NULL || strstr(outfile_template, sdruno_placeholder) != NULL) {
            fprintf(stderr, "output type SDRconnect cannot have '{WAVVIEWDX-RAW}' or '{SDRUNO}' in the output filename\n");
            return -1;
        }
    }
    return 0;
}

static int generate_output_filename(char *output_filename, int output_filename_max_size) {
    const char wavviewdx_raw_placeholder[] = "{WAVVIEWDX-RAW}";
    int wavviewdx_raw_placeholder_len = sizeof(wavviewdx_raw_placeholder) - 1;
    const char sdruno_placeholder[] = "{SDRUNO}";
    int sdruno_placeholder_len = sizeof(sdruno_placeholder) - 1;
    const char sdrconnect_placeholder[] = "{SDRCONNECT}";
    int sdrconnect_placeholder_len = sizeof(sdrconnect_placeholder) - 1;
    const char freq_placeholder[] = "{FREQ}";
    int freq_placeholder_len = sizeof(freq_placeholder) - 1;
    const char freqhz_placeholder[] = "{FREQHZ}";
    int freqhz_placeholder_len = sizeof(freqhz_placeholder) - 1;
    const char freqkhz_placeholder[] = "{FREQKHZ}";
    int freqkhz_placeholder_len = sizeof(freqkhz_placeholder) - 1;
    const char timestamp_placeholder[] = "{TIMESTAMP}";
    int timestamp_placeholder_len = sizeof(timestamp_placeholder) - 1;
    const char tsiso8601_placeholder[] = "{TSISO8601}";
    int tsiso8601_placeholder_len = sizeof(tsiso8601_placeholder) - 1;
    const char localtime_placeholder[] = "{LOCALTIME}";
    int localtime_placeholder_len = sizeof(localtime_placeholder) - 1;

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
        if (strncmp(src, wavviewdx_raw_placeholder, wavviewdx_raw_placeholder_len) == 0) {
            sz = dstlast - dst;
            char tsbuf[16];
            if (*(src + wavviewdx_raw_placeholder_len) == 'z') {
                strftime(tsbuf, sizeof(tsbuf), "%Y%m%d-%H%M%S", tm);
            } else {
                struct tm *localtm = localtime(&t);
                strftime(tsbuf, sizeof(tsbuf), "%Y%m%d-%H%M%S", localtm);
            }
            size_t nwvdr = snprintf(dst, sz, "iq_pcm16_ch%d_cf%.0lf_sr%.0lf_dt%s", is_dual_tuner ? 2 :1, frequency_A, output_sample_rate, tsbuf);
            if (nwvdr >= sz)
                return -1;
            src += wavviewdx_raw_placeholder_len;
            dst += nwvdr;
        } else if (strncmp(src, sdruno_placeholder, sdruno_placeholder_len) == 0) {
            sz = dstlast - dst;
            char tsbuf[16];
            strftime(tsbuf, sizeof(tsbuf), "%Y%m%d_%H%M%S", tm);
            size_t nsu = snprintf(dst, sz, "SDRuno_%sZ_%.0lfkHz", tsbuf, frequency_A / 1e3);
            if (nsu >= sz)
                return -1;
            src += sdruno_placeholder_len;
            dst += nsu;
        } else if (strncmp(src, sdrconnect_placeholder, sdrconnect_placeholder_len) == 0) {
            sz = dstlast - dst;
            struct tm *localtm = localtime(&t);
            char tsbuf[16];
            strftime(tsbuf, sizeof(tsbuf), "%Y%m%d_%H%M%S", localtm);
            size_t nsc = snprintf(dst, sz, "SDRconnect_IQ_%s_%.0lfHZ", tsbuf, frequency_A);
            if (nsc >= sz)
                return -1;
            src += sdrconnect_placeholder_len;
            dst += nsc;
        } else if (strncmp(src, freq_placeholder, freq_placeholder_len) == 0) {
            sz = dstlast - dst;
            size_t nf;
            if (!is_dual_tuner || frequency_A == frequency_B) {
                nf = snprintf(dst, sz, "%.0lf", frequency_A);
            } else {
                nf = snprintf(dst, sz, "%.0lf-%.0lf", frequency_A, frequency_B);
            }
            if (nf >= sz)
                return -1;
            src += freq_placeholder_len;
            dst += nf;
        } else if (strncmp(src, freqhz_placeholder, freqhz_placeholder_len) == 0) {
            sz = dstlast - dst;
            size_t nf;
            if (!is_dual_tuner || frequency_A == frequency_B) {
                nf = snprintf(dst, sz, "%.0lfHz", frequency_A);
            } else {
                nf = snprintf(dst, sz, "%.0lfHz-%.0lfHz", frequency_A, frequency_B);
            }
            if (nf >= sz)
                return -1;
            src += freqhz_placeholder_len;
            dst += nf;
        } else if (strncmp(src, freqkhz_placeholder, freqkhz_placeholder_len) == 0) {
            sz = dstlast - dst;
            size_t nf;
            if (!is_dual_tuner || frequency_A == frequency_B) {
                nf = snprintf(dst, sz, "%.0lfkHz", frequency_A / 1e3);
            } else {
                nf = snprintf(dst, sz, "%.0lfkHz-%.0lfkHz", frequency_A / 1e3, frequency_B / 1e3);
            }
            if (nf >= sz)
                return -1;
            src += freqkhz_placeholder_len;
            dst += nf;
        } else if (strncmp(src, timestamp_placeholder, timestamp_placeholder_len) == 0) {
            sz = dstlast - dst;
            size_t nts = strftime(dst, sz, "%Y%m%d_%H%M%SZ", tm);
            if (nts == 0)
                return -1;
            src += timestamp_placeholder_len;
            dst += nts;
        } else if (strncmp(src, tsiso8601_placeholder, tsiso8601_placeholder_len) == 0) {
            sz = dstlast - dst;
            size_t nts = strftime(dst, sz, "%Y%m%dT%H%M%SZ", tm);
            if (nts == 0)
                return -1;
            src += tsiso8601_placeholder_len;
            dst += nts;
        } else if (strncmp(src, localtime_placeholder, localtime_placeholder_len) == 0) {
            sz = dstlast - dst;
            struct tm *localtm = localtime(&t);
            size_t nlt = strftime(dst, sz, "%Y%m%d_%H%M%S%z", localtm);
            if (nlt == 0)
                return -1;
            src += localtime_placeholder_len;
            dst += nlt;
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
