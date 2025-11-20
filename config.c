/* record to file the I/Q stream(s) from a SDRplay RSP
 * get config from command line arguments and cofig file(s)
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"
#include "constants.h"
#include "typedefs.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* defaults */
static const sdrplay_api_AgcControlT default_agc_enabled_setting = sdrplay_api_AGC_50HZ;
static char default_output_filename_raw[] = "RSP_recording_{TIMESTAMP}_{FREQKHZ}.iq";
static char default_output_filename_linrad[] = "RSP_recording_{TIMESTAMP}_{FREQKHZ}.raw";
static char default_output_filename_wav[] = "RSP_recording_{TIMESTAMP}_{FREQHZ}.wav";


/* global variables */
/* RSP settings */
const char *serial_number = NULL;
sdrplay_api_RspDuoModeT rspduo_mode = sdrplay_api_RspDuoMode_Unknown;
const char *antenna = NULL;
double sample_rate = 0.0;
int decimation = 1;
sdrplay_api_If_kHzT if_frequency = sdrplay_api_IF_Zero;
sdrplay_api_Bw_MHzT if_bandwidth = sdrplay_api_BW_0_200;
sdrplay_api_AgcControlT agc_A = sdrplay_api_AGC_DISABLE;
sdrplay_api_AgcControlT agc_B = sdrplay_api_AGC_DISABLE;
int gRdB_A = 40;
int gRdB_B = 40;
int LNAstate_A = 0;
int LNAstate_B = 0;
int RFNotch = 0;
int DABNotch = 0;
int rspDuoAMNotch = 0;
int DCenable = 1;
int IQenable = 1;
int dcCal = 3;
int speedUp = 0;
int trackTime = 1;
int refreshRateTime = 2048;
int biasTEnable = 0;
int HDRmode = 0;
sdrplay_api_RspDx_HdrModeBwT hdr_mode_bandwidth = sdrplay_api_RspDx_HDRMODE_BW_1_700;
double frequency_A = 100e6;
double frequency_B = 100e6;
/* streaming and output settings */
int streaming_time = 10;  /* streaming time in seconds */
int marker_interval = 0;  /* store a marker tick every N seconds */
char *outfile_template = NULL;
OutputType output_type = OUTPUT_TYPE_RAW;
unsigned int zero_sample_gaps_max_size = 100000;
#ifndef WIN32
unsigned int blocks_buffer_capacity = 2000;
unsigned int samples_buffer_capacity = 1048576;
#else
/* Windows requires larger buffers */
unsigned int blocks_buffer_capacity = 16000;
unsigned int samples_buffer_capacity = 8388608;
#endif
/* gain file */
int gains_file_enable = 0;
int gain_changes_buffer_capacity = 100;
/* misc settings */
int debug_enable = 0;
int verbose = 0;


/* internal functions */
static int read_config_file(const char *config_file);


static void usage(const char* progname)
{
    fprintf(stderr, "usage: %s [options...]\n", progname);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "    -c <confiiguration file>\n");
    fprintf(stderr, "    -s <RSP serial number>\n");
    fprintf(stderr, "    -t <RSPduo mode> (1: single tuner, 2: dual tuner, 4: master, 8: slave\n");
    fprintf(stderr, "    -a <antenna>\n");
    fprintf(stderr, "    -r <RSP sample rate>\n");
    fprintf(stderr, "    -d <decimation>\n");
    fprintf(stderr, "    -i <IF frequency>\n");
    fprintf(stderr, "    -b <IF bandwidth>\n");
    fprintf(stderr, "    -g <IF gain reduction> (\"AGC\" to enable AGC)\n");
    fprintf(stderr, "    -l <LNA state>\n");
    fprintf(stderr, "    -n <notch filter> (one of: RF, DAB, or RSPduo-AM)\n");
    fprintf(stderr, "    -D disable post tuner DC offset compensation (default: enabled)\n");
    fprintf(stderr, "    -I disable post tuner I/Q balance compensation (default: enabled)\n");
    fprintf(stderr, "    -y tuner DC offset compensation parameters <dcCal,speedUp,trackTime,refeshRateTime> (default: 3,0,1,2048)\n");
    fprintf(stderr, "    -B enable bias-T\n");
    fprintf(stderr, "    -H enable HDR mode for RSPdx and RSPdx-R2\n");
    fprintf(stderr, "    -u <HDR mode bandwidth> (0: 200kHz, 1: 500kHz, 2: 1200kHz, 3: 1700kHz)\n");
    fprintf(stderr, "    -f <center frequency>\n");
    fprintf(stderr, "    -x <streaming time (s)> (default: 10s)\n");
    fprintf(stderr, "    -m <time marker interval (s)> (default: 0 -> no time markers)\n");
    fprintf(stderr, "    -o <output filename template>\n");
    fprintf(stderr, "    -z <zero sample gaps if smaller than size> (default: 100000)\n");
    fprintf(stderr, "    -j <blocks buffer capacity> (in number of blocks)\n");
    fprintf(stderr, "    -k <samples buffer capacity> (in number of samples)\n");
    fprintf(stderr, "    -L output file in Linrad format\n");
    fprintf(stderr, "    -R output file in raw format (i.e. just the samples)\n");
    fprintf(stderr, "    -W output file in RIFF/RF64 format\n");
    fprintf(stderr, "    -G write gains file (default: disabled)\n");
    fprintf(stderr, "    -X enable SDRplay API debug log level (default: disabled)\n");
    fprintf(stderr, "    -v enable verbose mode (default: disabled)\n");
    fprintf(stderr, "    -h show usage\n");
}


int get_config_from_cli(int argc, char *argv[])
{
    int c;
    while ((c = getopt(argc, argv, "c:s:t:a:r:d:i:b:g:l:n:DIy:BHu:f:x:m:o:z:j:k:RLWGXvh")) != -1) {
        int n;
        switch (c) {
            case 'c':
                if (read_config_file(optarg) != 0) {
                    fprintf(stderr, "error reading config file %s\n", optarg);
                    return -1;
                }
                break;
            case 's':
                serial_number = optarg;
                break;
            case 't':
                if (sscanf(optarg, "%d", (int *)(&rspduo_mode)) != 1) {
                    fprintf(stderr, "invalid RSPduo mode: %s\n", optarg);
                    return -1;
                }
                break;
            case 'a':
                antenna = optarg;
                break;
            case 'r':
                if (sscanf(optarg, "%lg", &sample_rate) != 1) {
                    fprintf(stderr, "invalid sample rate: %s\n", optarg);
                    return -1;
                }
                break;
            case 'd':
                if (sscanf(optarg, "%d", &decimation) != 1) {
                    fprintf(stderr, "invalid decimation: %s\n", optarg);
                    return -1;
                }
                break;
            case 'i':
                if (sscanf(optarg, "%d", (int *)(&if_frequency)) != 1) {
                    fprintf(stderr, "invalid IF frequency: %s\n", optarg);
                    return -1;
                }
                break;
            case 'b':
                if (sscanf(optarg, "%d", (int *)(&if_bandwidth)) != 1) {
                    fprintf(stderr, "invalid IF bandwidth: %s\n", optarg);
                    return -1;
                }
                break;
            case 'g':
                if (strcmp(optarg, "AGC") == 0 || strcmp(optarg, "AGC,AGC") == 0) {
                    agc_A = default_agc_enabled_setting;
                    agc_B = default_agc_enabled_setting;
                } else if (sscanf(optarg, "AGC,%d", &gRdB_B) == 1) {
                    agc_A = default_agc_enabled_setting;
                    agc_B = sdrplay_api_AGC_DISABLE;
                } else if (sscanf(optarg, "%d,AGC", &gRdB_A) == 1) {
                    agc_A = sdrplay_api_AGC_DISABLE;
                    agc_B = default_agc_enabled_setting;
                } else {
                    n = sscanf(optarg, "%d,%d", &gRdB_A, &gRdB_B);
                    if (n < 1) {
                        fprintf(stderr, "invalid IF gain reduction: %s\n", optarg);
                        return -1;
                    }
                    if (n == 1) {
                        gRdB_B = gRdB_A;
                    }
                    agc_A = sdrplay_api_AGC_DISABLE;
                    agc_B = sdrplay_api_AGC_DISABLE;
                }
                break;
            case 'l':
                n = sscanf(optarg, "%d,%d", &LNAstate_A, &LNAstate_B);
                if (n < 1) {
                    fprintf(stderr, "invalid LNA state: %s\n", optarg);
                    return -1;
                }
                if (n == 1) {
                    LNAstate_B = LNAstate_A;
                }
                break;
            case 'n':
                if (strcasecmp(optarg, "RF") == 0 || strcasecmp(optarg, "FM") == 0) {
                    RFNotch = 1;
                } else if (strcasecmp(optarg, "DAB") == 0) {
                    DABNotch = 1;
                } else if (strcasecmp(optarg, "RSPduo-AM") == 0) {
                    rspDuoAMNotch = 1;
                } else {
                    fprintf(stderr, "invalid notch selection: %s\n", optarg);
                    return -1;
                }
                break;
            case 'D':
                DCenable = 0;
                break;
            case 'I':
                IQenable = 0;
                break;
            case 'y':
                if (sscanf(optarg, "%d,%d,%d,%d", &dcCal, &speedUp, &trackTime, &refreshRateTime) != 4) {
                    fprintf(stderr, "invalid tuner DC offset compensation parameters: %s\n", optarg);
                    return -1;
                }
                break;
            case 'B':
                biasTEnable = 1;
                break;
            case 'H':
                HDRmode = 1;
                break;
            case 'u':
                if (sscanf(optarg, "%d", (int *)(&hdr_mode_bandwidth)) != 1) {
                    fprintf(stderr, "invalid HDR mode bandwidth: %s\n", optarg);
                    return -1;
                }
                break;
            case 'f':
                n = sscanf(optarg, "%lg,%lg", &frequency_A, &frequency_B);
                if (n < 1) {
                    fprintf(stderr, "invalid frequency: %s\n", optarg);
                    return -1;
                }
                if (n == 1)
                    frequency_B = frequency_A;
                break;
            case 'x':
                if (sscanf(optarg, "%d", &streaming_time) != 1) {
                    fprintf(stderr, "invalid streaming time: %s\n", optarg);
                    return -1;
                }
                break;
            case 'm':
                if (sscanf(optarg, "%d", &marker_interval) != 1) {
                    fprintf(stderr, "invalid marker interval: %s\n", optarg);
                    return -1;
                }
                break;
            case 'o':
                outfile_template = optarg;
                break;
            case 'z':
                if (sscanf(optarg, "%u", &zero_sample_gaps_max_size) != 1) {
                    fprintf(stderr, "invalid zero sample gaps max size: %s\n", optarg);
                    return -1;
                }
                break;
            case 'j':
                if (sscanf(optarg, "%u", &blocks_buffer_capacity) != 1) {
                    fprintf(stderr, "invalid blocks buffer capacity: %s\n", optarg);
                    return -1;
                }
                break;
            case 'k':
                if (sscanf(optarg, "%u", &samples_buffer_capacity) != 1) {
                    fprintf(stderr, "invalid samples buffer capacity: %s\n", optarg);
                    return -1;
                }
                break;
            case 'R':
                output_type = OUTPUT_TYPE_RAW;
                break;
            case 'L':
                output_type = OUTPUT_TYPE_LINRAD;
                break;
            case 'W':
                output_type = OUTPUT_TYPE_WAV;
                break;
            case 'G':
                gains_file_enable = 1;
                break;
            case 'X':
                debug_enable = 1;
                break;
            case 'v':
                verbose = 1;
                break;

            // help
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
                break;
            case '?':
            default:
                usage(argv[0]);
                return -1;
        }
    }

    if (marker_interval > 0) {
        if (output_type != OUTPUT_TYPE_WAV) {
            fprintf(stderr, "time markers require WAV output types");
            return -1;
        }
    }
    if (4 * zero_sample_gaps_max_size > samples_buffer_capacity) {
        fprintf(stderr, "samples buffer is not large enough to accomodate zeroing sample gaps");
        return -1;
    }

    /* output file template */
    if (outfile_template == NULL) {
        switch (output_type) {
            case OUTPUT_TYPE_RAW:
                outfile_template = default_output_filename_raw;
                break;
            case OUTPUT_TYPE_LINRAD:
                outfile_template = default_output_filename_linrad;
                break;
            case OUTPUT_TYPE_WAV:
                outfile_template = default_output_filename_wav;
                break;
            default:
                return -1;
        }
    }

    return 0;
}

/* configuration file */
static FILE *config_fp = NULL;

static int read_config_file_begin(const char * path) {
    config_fp = fopen(path, "r");

    if (config_fp == NULL) {
        fprintf(stderr, "unable to open config file %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int read_config_file_iter(const char ** key, const char ** value) {
    static char line[LINE_BUFFER_SIZE];

    while (fgets(line, sizeof(line), config_fp) != NULL) {
        int startidx, endidx;
        startidx = 0;
        endidx = strlen(line);
        while (endidx > startidx && isspace(line[endidx-1]))
            endidx--;
        while (endidx > startidx && isspace(line[startidx]))
            startidx++;
        char * trimmed_line = line + startidx;
        line[endidx] = '\0';
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#')
            continue;
        char * sep = strchr(trimmed_line, '=');
        if (sep == NULL) {
            fprintf(stderr, "invalid line in config file file %s\n", trimmed_line);
            continue;
        }
        endidx = sep - trimmed_line;
        while (endidx > 0 && isspace(trimmed_line[endidx-1]))
            endidx--;
        char *keyx = trimmed_line;
        trimmed_line[endidx] = '\0';
        if (strlen(keyx) == 0) {
            fprintf(stderr, "empty key in config file - value: %s\n", sep+1);
            continue;
        }
        startidx = 1;
        endidx = strlen(sep);
        while (endidx > startidx && isspace(sep[startidx]))
            startidx++;
        char * valuex = sep + startidx;
        if (strlen(valuex) == 0) {
            fprintf(stderr, "empty value in config file - key: %s\n", keyx);
            continue;
        }
        *key = (const char *) keyx;
        *value = (const char *) valuex;
        return 0;
    }
    return EOF;
}

static void read_config_file_end() {
    fclose(config_fp);
}

static int read_config_bool(const char *valuestr, int *value) {
    if (strcasecmp(valuestr, "true") == 0 || strcasecmp(valuestr, "yes") == 0 || strcasecmp(valuestr, "enable") == 0 || strcmp(valuestr, "1") == 0) {
        *value = 1;
    } else if (strcasecmp(valuestr, "false") == 0 || strcasecmp(valuestr, "no") == 0 || strcasecmp(valuestr, "disable") == 0 || strcmp(valuestr, "0") == 0) {
        *value = 0;
    } else {
        return -1;
    }
    return 0; 
}

static int read_config_double(const char *valuestr, double *value) {
    double tmp;
    int n;
    if (sscanf(valuestr, "%lf%n", &tmp, &n) != 1 || (size_t)n != strlen(valuestr)) {
        return -1;
    }
    *value = tmp;
    return 0; 
}

static int read_config_int(const char *valuestr, int *value) {
    int tmp;
    int n;
    if (sscanf(valuestr, "%d%n", &tmp, &n) != 1 || (size_t)n != strlen(valuestr)) {
        return -1;
    }
    *value = tmp;
    return 0; 
}

static int read_config_unsigned_int(const char *valuestr, unsigned int *value) {
    unsigned int tmp;
    int n;
    if (sscanf(valuestr, "%u%n", &tmp, &n) != 1 || (size_t)n != strlen(valuestr)) {
        return -1;
    }
    *value = tmp;
    return 0; 
}

static int read_config_string(const char *valuestr, const char **value) {
    *value = strdup(valuestr);
    return 0; 
}

static int read_config_grdb(const char *valuestr, sdrplay_api_AgcControlT *agc_first, sdrplay_api_AgcControlT *agc_second, int *gRdB_first, int *gRdB_second) {
    char first[LINE_PARTS_BUFFER_SIZE];
    char separator[LINE_PARTS_BUFFER_SIZE];
    char second[LINE_PARTS_BUFFER_SIZE];
    int nn = sscanf(valuestr, "%[^, ]%[, ]%[^, ]", first, separator, second);
    if (!(nn == 1 || nn == 3 )) {
        return -1;
    }
    if (nn == 3 && !(strcmp(separator, " ") == 0 || strcmp(separator, ",") == 0 || strcmp(separator, ", ") == 0)) {
        return -1;
    }

    if (nn == 1) {
        if (strcasecmp(first, "AGC") == 0) {
            *agc_first = default_agc_enabled_setting;
            *agc_second = default_agc_enabled_setting;
        } else {
            int tmp;
            int n;
            if (sscanf(first, "%d%n", &tmp, &n) != 1 || (size_t)n != strlen(first)) {
                return -1;
            }
            *agc_first = sdrplay_api_AGC_DISABLE;
            *agc_second = sdrplay_api_AGC_DISABLE;
            *gRdB_first = tmp;
            *gRdB_second = tmp;
        }
    } else if (nn == 3) {
        sdrplay_api_AgcControlT tmp_agc_first;
        sdrplay_api_AgcControlT tmp_agc_second;
        int tmp_gRdB_first;
        int tmp_gRdB_second;
        if (strcasecmp(first, "AGC") == 0) {
            tmp_agc_first = default_agc_enabled_setting;
        } else {
            int n;
            if (sscanf(first, "%d%n", &tmp_gRdB_first, &n) != 1 || (size_t)n != strlen(first)) {
                return -1;
            }
            tmp_agc_first = sdrplay_api_AGC_DISABLE;
        }
        if (strcasecmp(second, "AGC") == 0) {
            tmp_agc_second = default_agc_enabled_setting;
        } else {
            int n;
            if (sscanf(second, "%d%n", &tmp_gRdB_second, &n) != 1 || (size_t)n != strlen(second)) {
                return -1;
            }
            tmp_agc_second = sdrplay_api_AGC_DISABLE;
        }

        *agc_first = tmp_agc_first;
        *agc_second = tmp_agc_second;
        *gRdB_first = tmp_gRdB_first;
        *gRdB_second = tmp_gRdB_second;
    }

    return 0; 
}

static int read_config_two_doubles(const char *valuestr, double *value_first, double *value_second) {
    char first[LINE_PARTS_BUFFER_SIZE];
    char separator[LINE_PARTS_BUFFER_SIZE];
    char second[LINE_PARTS_BUFFER_SIZE];
    int nn = sscanf(valuestr, "%[^, ]%[, ]%[^, ]", first, separator, second);
    if (!(nn == 1 || nn == 3 )) {
        return -1;
    }
    if (nn == 3 && !(strcmp(separator, " ") == 0 || strcmp(separator, ",") == 0 || strcmp(separator, ", ") == 0)) {
        return -1;
    }

    if (nn == 1) {
        double tmp;
        int n;
        if (sscanf(first, "%lf%n", &tmp, &n) != 1 || (size_t)n != strlen(first)) {
            return -1;
        }
        *value_first = tmp;
        *value_second = tmp;
    } else if (nn == 3) {
        double tmp_first;
        double tmp_second;
        int n;
        if (sscanf(first, "%lf%n", &tmp_first, &n) != 1 || (size_t)n != strlen(first)) {
            return -1;
        }
        if (sscanf(second, "%lf%n", &tmp_second, &n) != 1 || (size_t)n != strlen(second)) {
            return -1;
        }
        *value_first = tmp_first;
        *value_second = tmp_second;
    }

    return 0; 
}

static int read_config_two_ints(const char *valuestr, int *value_first, int *value_second) {
    char first[LINE_PARTS_BUFFER_SIZE];
    char separator[LINE_PARTS_BUFFER_SIZE];
    char second[LINE_PARTS_BUFFER_SIZE];
    int nn = sscanf(valuestr, "%[^, ]%[, ]%[^, ]", first, separator, second);
    if (!(nn == 1 || nn == 3 )) {
        return -1;
    }
    if (nn == 3 && !(strcmp(separator, " ") == 0 || strcmp(separator, ",") == 0 || strcmp(separator, ", ") == 0)) {
        return -1;
    }

    if (nn == 1) {
        int tmp;
        int n;
        if (sscanf(first, "%d%n", &tmp, &n) != 1 || (size_t)n != strlen(first)) {
            return -1;
        }
        *value_first = tmp;
        *value_second = tmp;
    } else if (nn == 3) {
        int tmp_first;
        int tmp_second;
        int n;
        if (sscanf(first, "%d%n", &tmp_first, &n) != 1 || (size_t)n != strlen(first)) {
            return -1;
        }
        if (sscanf(second, "%d%n", &tmp_second, &n) != 1 || (size_t)n != strlen(second)) {
            return -1;
        }
        *value_first = tmp_first;
        *value_second = tmp_second;
    }

    return 0; 
}

static int read_config_output_type(const char *valuestr, OutputType *value) {
    if (strcasecmp(valuestr, "raw") == 0) {
        *value = OUTPUT_TYPE_RAW;
    } else if (strcasecmp(valuestr, "linrad") == 0) {
        *value = OUTPUT_TYPE_LINRAD;
    } else if (strcasecmp(valuestr, "wav") == 0 || strcasecmp(valuestr, "rf64") == 0) {
        *value = OUTPUT_TYPE_WAV;
    } else {
        return -1;
    }
    return 0;
}

static int read_config_file(const char *config_file) {
    if (read_config_file_begin(config_file) == -1)
        return -1;
    const char *key;
    const char *value;
    int status = 0;
    while (read_config_file_iter(&key, &value) != EOF) {
        int read_config_status = 0;
        if (strcasecmp(key, "serial number") == 0) {
            read_config_status = read_config_string(value, &serial_number);
        } else if (strcasecmp(key, "RSPduo mode") == 0) {
            read_config_status = read_config_int(value, (int *)(&rspduo_mode));
        } else if (strcasecmp(key, "antenna") == 0) {
            read_config_status = read_config_string(value, &antenna);
        } else if (strcasecmp(key, "sample rate") == 0 || strcasecmp(key, "RSP sample rate") == 0) {
            read_config_status = read_config_double(value, &sample_rate);
        } else if (strcasecmp(key, "decimation") == 0) {
            read_config_status = read_config_int(value, &decimation);
        } else if (strcasecmp(key, "IF frequency") == 0) {
            read_config_status = read_config_int(value, (int *)(&if_frequency));
        } else if (strcasecmp(key, "IF bandwidth") == 0) {
            read_config_status = read_config_int(value, (int *)(&if_bandwidth));
        } else if (strcasecmp(key, "gRdB") == 0 || strcasecmp(key, "IFGR") == 0) {
            read_config_status = read_config_grdb(value, &agc_A, &agc_B, &gRdB_A, &gRdB_B);
        } else if (strcasecmp(key, "LNA state") == 0 || strcasecmp(key, "RFGR") == 0) {
            read_config_status = read_config_two_ints(value, &LNAstate_A, &LNAstate_B);
        } else if (strcasecmp(key, "RF notch") == 0 || strcasecmp(key, "FM notch") == 0) {
            read_config_status = read_config_bool(value, &RFNotch);
        } else if (strcasecmp(key, "DAB notch") == 0) {
            read_config_status = read_config_bool(value, &DABNotch);
        } else if (strcasecmp(key, "RSPduo AM notch") == 0) {
            read_config_status = read_config_bool(value, &rspDuoAMNotch);
        } else if (strcasecmp(key, "DC offset correction") == 0 || strcasecmp(key, "DC corr") == 0) {
            read_config_status = read_config_bool(value, &DCenable);
        } else if (strcasecmp(key, "IQ imbalance correction") == 0 || strcasecmp(key, "IQ corr") == 0) {
            read_config_status = read_config_bool(value, &IQenable);
        } else if (strcasecmp(key, "DC offset dcCal") == 0 || strcasecmp(key, "dcCal") == 0) {
            read_config_status = read_config_int(value, &dcCal);
        } else if (strcasecmp(key, "DC offset speedUp") == 0 || strcasecmp(key, "speedUp") == 0) {
            read_config_status = read_config_int(value, &speedUp);
        } else if (strcasecmp(key, "DC offset trackTime") == 0 || strcasecmp(key, "trackTime") == 0) {
            read_config_status = read_config_int(value, &trackTime);
        } else if (strcasecmp(key, "DC offset refreshRateTime") == 0 || strcasecmp(key, "refreshRateTime") == 0) {
            read_config_status = read_config_int(value, &refreshRateTime);
        } else if (strcasecmp(key, "Bias-T") == 0 || strcasecmp(key, "BiasT") == 0) {
            read_config_status = read_config_bool(value, &biasTEnable);
        } else if (strcasecmp(key, "HDR mode") == 0) {
            read_config_status = read_config_bool(value, &HDRmode);
        } else if (strcasecmp(key, "HD mode bandwidth") == 0) {
            read_config_status = read_config_int(value, (int *)(&hdr_mode_bandwidth));
        } else if (strcasecmp(key, "frequency") == 0) {
            read_config_status = read_config_two_doubles(value, &frequency_A, &frequency_B);
        } else if (strcasecmp(key, "streaming time") == 0) {
            read_config_status = read_config_int(value, &streaming_time);
        } else if (strcasecmp(key, "marker interval") == 0) {
            read_config_status = read_config_int(value, &marker_interval);
        } else if (strcasecmp(key, "output file") == 0) {
            read_config_status = read_config_string(value, (const char **)(&outfile_template));
        } else if (strcasecmp(key, "output type") == 0) {
            read_config_status = read_config_output_type(value, &output_type);
        } else if (strcasecmp(key, "gains file") == 0) {
            read_config_status = read_config_bool(value, &gains_file_enable);
        } else if (strcasecmp(key, "zero sample gaps max size") == 0) {
            read_config_status = read_config_unsigned_int(value, &zero_sample_gaps_max_size);
        } else if (strcasecmp(key, "blocks buffer capacity") == 0) {
            read_config_status = read_config_unsigned_int(value, &blocks_buffer_capacity);
        } else if (strcasecmp(key, "samples buffer capacity") == 0) {
            read_config_status = read_config_unsigned_int(value, &samples_buffer_capacity);
        } else if (strcasecmp(key, "gain changes buffer capacity") == 0) {
            read_config_status = read_config_int(value, &gain_changes_buffer_capacity);
        } else if (strcasecmp(key, "verbose") == 0) {
            read_config_status = read_config_bool(value, &verbose);
        } else {
            fprintf(stderr, "unknown key %s\n", key);
            status = -1;
        }
        if (read_config_status != 0) {
            fprintf(stderr, "invalid value for %s: %s\n", key, value);
            status = -1;
        }
    }
    read_config_file_end();
    return status;
}
