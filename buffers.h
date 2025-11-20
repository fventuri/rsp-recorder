/* record to file the I/Q stream(s) from a SDRplay RSP
 * buffers and resources
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _BUFFERS_H
#define _BUFFERS_H

#include <pthread.h>
#include <stdint.h>

/* typedefs */
typedef struct {
    unsigned int first_sample_num;
    unsigned int num_samples;
    unsigned int samples_index;
    char rx_id;
} BlockDescriptor;

typedef struct {
    pthread_mutex_t *lock;
    void *resource;
    unsigned int read_index;
    unsigned int write_index;
    unsigned int size;
    unsigned int nused;
    unsigned int nused_max;
    unsigned int nready;
    pthread_cond_t *is_ready;
} ResourceDescriptor;

typedef struct {
   struct timespec ts;
   unsigned long long sample_num;
} TimeMarker;

typedef struct {
    struct timespec start_ts;
    struct timespec stop_ts;
    TimeMarker *markers;
    time_t timetick_curr;
    int marker_interval;
    int markers_curr_idx;
    int markers_max_idx;
} TimeInfo;

typedef struct {
    uint64_t sample_num;
    float currGain;
    uint8_t tuner;
    uint8_t gRdB;
    uint8_t lnaGRdB;
    uint8_t unused;
} GainChange;

/* global variables */
extern ResourceDescriptor blocks_resource;
extern ResourceDescriptor samples_resource;
extern short *insamples;
extern TimeInfo timeinfo; 
extern ResourceDescriptor gain_changes_resource;

/* public functions */
int buffers_create();
void buffers_free();

#endif /* _BUFFERS_H */
