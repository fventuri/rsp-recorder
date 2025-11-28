/* record to file the I/Q stream(s) from a SDRplay RSP
 * callback functions
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _CALLBACKS_H
#define _CALLBACKS_H

#include "buffers.h"
#include "stats.h"

#include <time.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <sdrplay_api.h>
#pragma GCC diagnostic pop

/* typedefs */
typedef struct {
    unsigned int next_sample_num;
    int internal_decimation;
    ResourceDescriptor *blocks_resource;
    ResourceDescriptor *samples_resource;
    TimeInfo *timeinfo;
    RXStats *rx_stats;
} RXContext;

typedef struct {
    ResourceDescriptor *gain_changes_resource;
    unsigned long long *total_samples[2];
} EventContext;

typedef struct {
    RXContext *rx_contexts[2];
    EventContext *event_context;
} CallbackContext;

/* global variables */
extern unsigned long long num_gain_changes[2];
extern unsigned long long num_power_overload_detected[2];
extern unsigned long long num_power_overload_corrected[2];

/* public functions */
void rxA_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
void rxB_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext);

#endif /* _CALLBACKS_H */
