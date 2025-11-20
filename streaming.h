/* record to file the I/Q stream(s) from a SDRplay RSP
 * streaming
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _STREAMING_H
#define _STREAMING_H

#include "buffers.h"

#include <time.h>

/* typedefs */
typedef enum {
    STREAMING_STATUS_UNKNOWN,
    STREAMING_STATUS_STARTING,
    STREAMING_STATUS_RUNNING,
    STREAMING_STATUS_TERMINATE,
    STREAMING_STATUS_DONE,
    STREAMING_STATUS_FAILED,
    STREAMING_STATUS_BLOCKS_BUFFER_FULL,
    STREAMING_STATUS_SAMPLES_BUFFER_FULL,
    STREAMING_STATUS_GAIN_CHANGES_BUFFER_FULL,
} StreamingStatus;

/* global variables */
extern StreamingStatus streaming_status;

/* public functions */
int stream();

#endif /* _STREAMING_H */
