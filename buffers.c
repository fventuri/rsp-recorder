/* record to file the I/Q stream(s) from a SDRplay RSP
 * buffers and resources
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "buffers.h"
#include "config.h"
#include "rsp-recorder.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


/* global variables */
ResourceDescriptor blocks_resource;
ResourceDescriptor samples_resource;
short *insamples = NULL;
TimeInfo timeinfo;
ResourceDescriptor gain_changes_resource;


static BlockDescriptor *blocks;
static TimeMarker *markers = NULL;
static GainChange *gain_changes = NULL;
static bool is_blocks_buffer_allocated = false;
static bool is_insamples_buffer_allocated = false;
static bool is_time_markers_buffer_allocated = false;
static bool is_gain_changes_buffer_allocated = false;

static pthread_mutex_t blocks_lock;
static pthread_cond_t is_ready;
static pthread_mutex_t samples_lock;
static pthread_mutex_t gain_changes_lock;


int buffers_create() {
    int errcode;

    errcode = pthread_mutex_init(&blocks_lock, NULL);
    if (errcode != 0) {
        fprintf(stderr, "pthread_mutex_init(blocks_lock) failed - errcode=%d\n", errcode);
        return -1;
    }
    errcode = pthread_cond_init(&is_ready, NULL);
    if (errcode != 0) {
        fprintf(stderr, "pthread_cond_init(is_ready) failed - errcode=%d\n", errcode);
        return -1;
    }
    blocks = (BlockDescriptor *)malloc(blocks_buffer_capacity * sizeof(BlockDescriptor));
    if (blocks == NULL) {
        fprintf(stderr, "malloc(blocks) failed\n");
        return -1;
    }
    is_blocks_buffer_allocated = true;
    blocks_resource = (ResourceDescriptor) {
        .lock = &blocks_lock,
        .resource = blocks,
        .read_index = 0,
        .write_index = 0,
        .size = blocks_buffer_capacity,
        .nused = 0,
        .nused_max = 0,
        .nready = 0,
        .is_ready = &is_ready,
    };

    errcode = pthread_mutex_init(&samples_lock, NULL);
    if (errcode != 0) {
        fprintf(stderr, "pthread_mutex_init(samples_lock) failed - errcode=%d\n", errcode);
        return -1;
    }
    insamples = (short *)malloc(samples_buffer_capacity * sizeof(short));
    if (insamples == NULL) {
        fprintf(stderr, "malloc(insamples) failed\n");
        return -1;
    }
    is_insamples_buffer_allocated = true;
    samples_resource = (ResourceDescriptor) {
        .lock = &samples_lock,
        .resource = insamples,
        .read_index = 0,
        .write_index = 0,
        .size = samples_buffer_capacity,
        .nused = 0,
        .nused_max = 0,
        .nready = 0,
        .is_ready = NULL,
    };

    int markers_max_idx = 0;
    if (marker_interval > 0) {
        /* add two extra slots to also store the start time marker and
         * because integer division truncates
        */
        markers_max_idx = streaming_time / marker_interval + 2;
        markers = (TimeMarker *)malloc(markers_max_idx * sizeof(TimeMarker));
        if (markers == NULL) {
            markers_max_idx = 0;
        }
        is_time_markers_buffer_allocated = true;
    }
    timeinfo = (TimeInfo) {
        .start_ts = {0L, 0L},
        .stop_ts = {0L, 0L},
        .markers = markers,
        .timetick_curr = 0L,
        .marker_interval = marker_interval,
        .markers_curr_idx = 0,
        .markers_max_idx = markers_max_idx,
    };

    unsigned int gain_changes_size = 0;
    if (gains_file_enable) {
        errcode = pthread_mutex_init(&gain_changes_lock, NULL);
        if (errcode != 0) {
            fprintf(stderr, "pthread_mutex_init(gain_changes_lock) failed - errcode=%d\n", errcode);
            return -1;
        }
        gain_changes_size = gain_changes_buffer_capacity;
        gain_changes = (GainChange *)malloc(gain_changes_buffer_capacity * sizeof(GainChange));
        if (gain_changes == NULL) {
            fprintf(stderr, "malloc(gain changes) failed\n");
            return -1;
        }
        is_gain_changes_buffer_allocated = true;
    }
    gain_changes_resource = (ResourceDescriptor) {
        .lock = &gain_changes_lock,
        .resource = gain_changes,
        .read_index = 0,
        .write_index = 0,
        .size = gain_changes_size,
        .nused = 0,
        .nused_max = 0,
        .nready = 0,
        .is_ready = NULL,
    };

    return 0;
}

void buffers_free() {
    if (is_gain_changes_buffer_allocated) {
        free(gain_changes);
        gain_changes = NULL;
        is_gain_changes_buffer_allocated = false;
    }
    if (is_time_markers_buffer_allocated) {
        free(markers);
        markers = NULL;
        is_time_markers_buffer_allocated = false;
    }
    if (is_insamples_buffer_allocated) {
        free(insamples);
        insamples = NULL;
        is_insamples_buffer_allocated = false;
    }
    if (is_blocks_buffer_allocated) {
        free(blocks);
        blocks = NULL;
        is_blocks_buffer_allocated = false;
    }
}
