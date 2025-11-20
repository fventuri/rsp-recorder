/* record to file the I/Q stream(s) from a SDRplay RSP
 * SDRplay RSP related functions
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _SDRPLAY_RSP_H
#define _SDRPLAY_RSP_H

#include <stdbool.h>

/* global variables */
extern bool is_dual_tuner;
extern int internal_decimation;
extern double output_sample_rate;

/* public functions */
int sdrplay_rsp_open();
void sdrplay_rsp_close();
int sdrplay_select_rsp();
int sdrplay_validate_settings();
int sdrplay_configure_rsp();
int sdrplay_start_streaming();
float sdrplay_get_current_gain(int tuner);

#endif /* _SDRPLAY_RSP_H */
