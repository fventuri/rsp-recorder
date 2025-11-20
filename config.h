/* record to file the I/Q stream(s) from a SDRplay RSP
 * config
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _CONFIG_H
#define _CONFIG_H

#include "typedefs.h"

#include <sdrplay_api.h>


/* global variables */
/* RSP settings */
extern const char *serial_number;
extern sdrplay_api_RspDuoModeT rspduo_mode;
extern const char *antenna;
extern double sample_rate;
extern int decimation;
extern sdrplay_api_If_kHzT if_frequency;
extern sdrplay_api_Bw_MHzT if_bandwidth;
extern sdrplay_api_AgcControlT agc_A;
extern sdrplay_api_AgcControlT agc_B;
extern int gRdB_A;
extern int gRdB_B;
extern int LNAstate_A;
extern int LNAstate_B;
extern int RFNotch;
extern int DABNotch;
extern int rspDuoAMNotch;
extern int DCenable;
extern int IQenable;
extern int dcCal;
extern int speedUp;
extern int trackTime;
extern int refreshRateTime;
extern int biasTEnable;
extern int HDRmode;
extern sdrplay_api_RspDx_HdrModeBwT hdr_mode_bandwidth;
extern double frequency_A;
extern double frequency_B;
/* streaming and output settings */
extern int streaming_time;       /* streaming time in seconds */
extern int marker_interval;      /* store a marker tick every N seconds */
extern char *outfile_template;
extern OutputType output_type;
extern unsigned int zero_sample_gaps_max_size;
extern unsigned int blocks_buffer_capacity;
extern unsigned int samples_buffer_capacity;
/* gain filr */
extern int gains_file_enable;
extern int gain_changes_buffer_capacity;
/* misc settings */
extern int debug_enable;
extern int verbose;


/* public functions */
int get_config_from_cli(int argc, char *argv[]);

#endif /* _CONFIG_H */
