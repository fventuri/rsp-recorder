/* record to file the I/Q stream(s) from a SDRplay RSP
 * stats
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _STATS_H
#define _STATS_H

/* typedefs */
typedef struct {
    unsigned long long data_size;
    unsigned long long output_samples;
    unsigned long long total_writes;
    unsigned long long total_write_elapsed;
    unsigned long long max_write_elapsed;
    unsigned long long full_writes;
    unsigned long long partial_writes;
    unsigned long long zero_writes;
} Stats;

typedef struct {
    struct timespec earliest_callback;
    struct timespec latest_callback;
    unsigned long long total_samples;
    unsigned long long dropped_samples;
    unsigned int num_samples_min;
    unsigned int num_samples_max;
    short imin;
    short imax;
    short qmin;
    short qmax;
} RXStats;

/* global variables */
extern Stats stats;
extern RXStats rx_stats_A;
extern RXStats rx_stats_B;

/* public functions */
int print_stats();

#endif /* _STATS_H */
