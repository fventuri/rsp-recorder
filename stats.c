/* record to file the I/Q stream(s) from a SDRplay RSP
 * stats
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "callbacks.h"
#include "sdrplay-rsp.h"
#include "stats.h"

#include <limits.h>
#include <stdio.h>

/* global variables */
Stats stats = {
    .data_size = 0,
    .output_samples = 0,
    .total_writes = 0,
    .total_write_elapsed = 0,
    .max_write_elapsed = 0,
    .full_writes = 0,
    .partial_writes = 0,
    .zero_writes = 0,
};

RXStats rx_stats_A = {
    .earliest_callback = {0, 0},
    .latest_callback = {0, 0},
    .total_samples = 0,
    .dropped_samples = 0,
    .num_samples_min = UINT_MAX,
    .num_samples_max = 0,
    .imin = SHRT_MAX,
    .imax = SHRT_MIN,
    .qmin = SHRT_MAX,
    .qmax = SHRT_MIN,
};
RXStats rx_stats_B = {
    .earliest_callback = {0, 0},
    .latest_callback = {0, 0},
    .total_samples = 0,
    .dropped_samples = 0,
    .num_samples_min = UINT_MAX,
    .num_samples_max = 0,
    .imin = SHRT_MAX,
    .imax = SHRT_MIN,
    .qmin = SHRT_MAX,
    .qmax = SHRT_MIN,
};


int print_stats() {
    if (!is_dual_tuner) {
        // single tuner case
        /* estimate actual sample rate */
        double elapsed_sec = (rx_stats_A.latest_callback.tv_sec - rx_stats_A.earliest_callback.tv_sec) + 1e-9 * (rx_stats_A.latest_callback.tv_nsec - rx_stats_A.earliest_callback.tv_nsec);
        double actual_sample_rate = (double)(rx_stats_A.total_samples) / elapsed_sec;
        fprintf(stderr, "total samples = %llu\n", rx_stats_A.total_samples);
        fprintf(stderr, "dropped samples = %llu\n", rx_stats_A.dropped_samples);
        fprintf(stderr, "elapsed time = %lf\n", elapsed_sec);
        fprintf(stderr, "actual sample rate = %.0lf\n", actual_sample_rate);
        fprintf(stderr, "I samples range = [%hd,%hd]\n", rx_stats_A.imin, rx_stats_A.imax);
        fprintf(stderr, "Q samples range = [%hd,%hd]\n", rx_stats_A.qmin, rx_stats_A.qmax);
        fprintf(stderr, "samples per rx_callback range = [%u,%u]\n", rx_stats_A.num_samples_min, rx_stats_A.num_samples_max);
        fprintf(stderr, "output samples = %llu\n", stats.output_samples);
        fprintf(stderr, "power overload detected events = %llu\n", num_power_overload_detected[0]);
        fprintf(stderr, "power overload corrected events = %llu\n", num_power_overload_corrected[0]);
        fprintf(stderr, "gain changes = %llu\n", num_gain_changes[0]);
    } else {
        /* dual tuner */
        /* estimate actual sample rate */
        double elapsed_sec_A = (rx_stats_A.latest_callback.tv_sec - rx_stats_A.earliest_callback.tv_sec) + 1e-9 * (rx_stats_A.latest_callback.tv_nsec - rx_stats_A.earliest_callback.tv_nsec);
        double actual_sample_rate_A = (double)(rx_stats_A.total_samples) / elapsed_sec_A;
        double elapsed_sec_B = (rx_stats_B.latest_callback.tv_sec - rx_stats_B.earliest_callback.tv_sec) + 1e-9 * (rx_stats_B.latest_callback.tv_nsec - rx_stats_B.earliest_callback.tv_nsec);
        double actual_sample_rate_B = (double)(rx_stats_B.total_samples) / elapsed_sec_B;
        fprintf(stderr, "total samples = %llu / %llu\n", rx_stats_A.total_samples, rx_stats_B.total_samples);
        fprintf(stderr, "dropped samples = %llu / %llu\n", rx_stats_A.dropped_samples, rx_stats_B.dropped_samples);
        fprintf(stderr, "elapsed time = %lf / %lf\n", elapsed_sec_A, elapsed_sec_B);
        fprintf(stderr, "actual sample rate = %.0lf / %.0lf\n", actual_sample_rate_A, actual_sample_rate_B);
        fprintf(stderr, "I samples range = [%hd,%hd] / [%hd,%hd]\n", rx_stats_A.imin, rx_stats_A.imax, rx_stats_B.imin, rx_stats_B.imax);
        fprintf(stderr, "Q samples range = [%hd,%hd] / [%hd,%hd]\n", rx_stats_A.qmin, rx_stats_A.qmax, rx_stats_B.qmin, rx_stats_B.qmax);
        fprintf(stderr, "samples per rx_callback range = [%u,%u] / [%u,%u]\n", rx_stats_A.num_samples_min, rx_stats_A.num_samples_max, rx_stats_B.num_samples_min, rx_stats_B.num_samples_max);
        fprintf(stderr, "output samples = %llu (x2)\n", stats.output_samples);
        fprintf(stderr, "power overload detected events = %llu / %llu\n", num_power_overload_detected[0], num_power_overload_detected[1]);
        fprintf(stderr, "power overload corrected events = %llu / %llu\n", num_power_overload_corrected[0], num_power_overload_corrected[1]);
        fprintf(stderr, "gain changes = %llu / %llu\n", num_gain_changes[0], num_gain_changes[1]);
    }
    fprintf(stderr, "data size = %llu\n", stats.data_size);
    fprintf(stderr, "blocks buffer usage = %u/%u\n", blocks_resource.nused_max, blocks_resource.size);
    fprintf(stderr, "samples buffer usage = %u/%u\n", samples_resource.nused_max, samples_resource.size);
    unsigned long long average_write_elapsed = stats.total_write_elapsed / stats.total_writes;
    fprintf(stderr, "average write elapsed = %llu.%09llu\n", average_write_elapsed / 1000000000ULL, average_write_elapsed % 1000000000ULL);
    fprintf(stderr, "max write elapsed = %llu.%09llu\n", stats.max_write_elapsed / 1000000000ULL, stats.max_write_elapsed % 1000000000ULL);
    fprintf(stderr, "total writes = %llu\n", stats.total_writes);
    fprintf(stderr, "full writes = %llu\n", stats.full_writes);
    fprintf(stderr, "partial writes = %llu\n", stats.partial_writes);
    fprintf(stderr, "zero writes = %llu\n", stats.zero_writes);

    return 0;
}
