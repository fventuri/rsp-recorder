/* record to file the I/Q stream(s) from a SDRplay RSP
 * streaming
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef WIN32
// timer queue
#include <windows.h>
#endif /* WIN32 */

#include "buffers.h"
#include "config.h"
#include "output.h"
#include "sdrplay-rsp.h"
#include "stats.h"
#include "streaming.h"

#define UNUSED(x) (void)(x)

// perhaps we need a mutex around streaming_status
StreamingStatus streaming_status = STREAMING_STATUS_STARTING;

/* internal functions */
static void signal_handler(int signum);
#ifdef WIN32
static VOID CALLBACK windows_timer_handler(PVOID lpParam, BOOLEAN TimerOrWaitFired)
#endif /* WIN32 */
static int next_block_descriptor_single(BlockDescriptor **pBlock);
static int next_block_descriptors_dual(BlockDescriptor **pBlockA, BlockDescriptor **pBlockB);
static int write_buffer(const uint8_t *buf, size_t count);
static void output_gain_changes();


int stream() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef WIN32
    signal(SIGALRM, signal_handler);
    alarm(streaming_time);
#else
    HANDLE timerQueue = CreateTimerQueue();
    if (timerQueue == NULL) {
        fprintf(stderr, "CreateTimerQueue failed - error=0x%lx\n", GetLastError());
        close(outfd);
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    } 
    HANDLE timer = NULL;
    if (!CreateTimerQueueTimer(&timer, timerQueue, (WAITORTIMERCALLBACK)windows_timer_handler, NULL, streaming_time * 1000, 0, 0)) {
        fprintf(stderr, "CreateTimerQueueTimer failed - error=0x%lx\n", GetLastError());   
        return -1;
    }
#endif /* WIN32 */

    unsigned int nrx = is_dual_tuner ? 2 : 1;

    if (verbose) {
        fprintf(stderr, "streaming for %d seconds\n", streaming_time);
    }

    streaming_status = STREAMING_STATUS_RUNNING;

    unsigned int next_sample_num = 0xffffffff;
    while (streaming_status == STREAMING_STATUS_RUNNING || streaming_status == STREAMING_STATUS_TERMINATE) {
        pthread_mutex_lock(blocks_resource.lock);
        while (blocks_resource.nready < nrx) {
            pthread_cond_wait(blocks_resource.is_ready, blocks_resource.lock);
        }
        pthread_mutex_unlock(blocks_resource.lock);
        while (blocks_resource.nready >= nrx) {
            pthread_mutex_lock(blocks_resource.lock);
            blocks_resource.nready -= nrx;
            pthread_mutex_unlock(blocks_resource.lock);

            BlockDescriptor *blockA = NULL;
            BlockDescriptor *blockB = NULL;
            if (!is_dual_tuner) {
                if (next_block_descriptor_single(&blockA) == -1) {
                    goto output_loop_finally;
                }
            } else {
                if (next_block_descriptors_dual(&blockA, &blockB) == -1) {
                    goto output_loop_finally;
                }
            }
            unsigned int first_sample_num = blockA->first_sample_num;
            unsigned int num_samples = blockA->num_samples;
            if (num_samples == 0) {
                streaming_status = STREAMING_STATUS_DONE;
                goto output_loop_finally;
            }
            unsigned int dropped_samples;
            if (!(next_sample_num == 0xffffffff || blockA->first_sample_num == next_sample_num)) {
                if (next_sample_num < first_sample_num) {
                    dropped_samples = first_sample_num - next_sample_num;
                } else {
                    dropped_samples = UINT_MAX - (first_sample_num - next_sample_num) + 1;
                }
                bool fill_gap_with_zeros = dropped_samples <= zero_sample_gaps_max_size;
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                fprintf(stderr, "%.24s - dropped %u samples - next_sample_num=%d first_sample_num=%u - %s\n", ctime(&ts.tv_sec), dropped_samples, next_sample_num, first_sample_num, fill_gap_with_zeros ? "filling gap with zeros" : "skipping gap");
                if (fill_gap_with_zeros) {
                    uint8_t *outdata = (uint8_t *)outsamples;
                    size_t bytes_left = dropped_samples * nrx * 2 * sizeof(short);
                    memset(outdata, 0, bytes_left);
                    if (write_buffer(outdata, bytes_left) == -1) {
                        goto output_loop_finally;
                    }
                    stats.output_samples += dropped_samples;
                }
            }
            unsigned int nsntmp = (first_sample_num + num_samples) * internal_decimation;
            next_sample_num = (nsntmp + (nsntmp % 4 < 2)) / internal_decimation;

            /* single tuner case:
             *     rearrange samples in pairs (I_A, Q_A)
             * dual tuner case:
             *     rearrange samples in 'quadruples' (I_A, Q_A, I_B, Q_B)
             */
            int values_per_sample = 2 * nrx;
            /* I tuner A */
            int inoffset = blockA->samples_index;
            int outoffset = 0;
            for (unsigned int i = 0; i < num_samples; i++, inoffset++, outoffset += values_per_sample) {
                outsamples[outoffset] = insamples[inoffset];
            }
            /* Q tuner A */
            inoffset = blockA->samples_index + num_samples;
            outoffset = 1;
            for (unsigned int i = 0; i < num_samples; i++, inoffset++, outoffset += values_per_sample) {
                outsamples[outoffset] = insamples[inoffset];
            }
            if (is_dual_tuner) {
                /* I tuner B */
                inoffset = blockB->samples_index;
                outoffset = 2;
                for (unsigned int i = 0; i < num_samples; i++, inoffset++, outoffset += values_per_sample) {
                    outsamples[outoffset] = insamples[inoffset];
                }
                /* Q tuner B */
                inoffset = blockB->samples_index + num_samples;
                outoffset = 3;
                for (unsigned int i = 0; i < num_samples; i++, inoffset++, outoffset += values_per_sample) {
                    outsamples[outoffset] = insamples[inoffset];
                }
            }

            uint8_t *outdata = (uint8_t *)outsamples;
            size_t bytes_left = num_samples * values_per_sample * sizeof(short);
            if (write_buffer(outdata, bytes_left) == -1) {
                goto output_loop_finally;
            }
            stats.output_samples += num_samples;

output_loop_finally:
            pthread_mutex_lock(blocks_resource.lock);
            blocks_resource.nused -= nrx;
            pthread_mutex_unlock(blocks_resource.lock);
            unsigned int num_samples_written = blockA->num_samples;
            if (is_dual_tuner) {
                num_samples_written += blockB->num_samples;
            }
            if (num_samples_written > 0) {
                pthread_mutex_lock(samples_resource.lock);
                /* multiply by 2 to take into account that both I and Q */
                samples_resource.nused -= 2 * num_samples_written;
                pthread_mutex_unlock(samples_resource.lock);
            }

            if (!(streaming_status == STREAMING_STATUS_RUNNING || streaming_status == STREAMING_STATUS_TERMINATE)) {
                break;
            }
        }

        if (gainsfd != -1) {
            output_gain_changes();
        }
    }
    return 0;
}

/* internal functions */
static void signal_handler(int signum) {
    UNUSED(signum);
    if (streaming_status == STREAMING_STATUS_RUNNING) {
        streaming_status = STREAMING_STATUS_TERMINATE;
    }
}
#ifdef WIN32
static VOID CALLBACK windows_timer_handler(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
    UNUSED(lpParam);
    UNUSED(TimerOrWaitFired);
    if (streaming_status == STREAMING_STATUS_RUNNING) {
        streaming_status = STREAMING_STATUS_TERMINATE;
    }
}
#endif /* WIN32 */

static int next_block_descriptor_single(BlockDescriptor **pBlock)
{
    unsigned int read_index = blocks_resource.read_index;
    BlockDescriptor *block = (BlockDescriptor *) blocks_resource.resource + read_index;
    read_index = (read_index + 1) % blocks_resource.size;
    blocks_resource.read_index = read_index;

    if (!(block->rx_id == 'A')) {
        fprintf(stderr, "invalid rx_id - %c\n", block->rx_id);
        streaming_status = STREAMING_STATUS_FAILED;
        return -1;
    }
    *pBlock = block;
    return 0;
}

static int next_block_descriptors_dual(BlockDescriptor **pBlockA, BlockDescriptor **pBlockB)
{
    unsigned int read_index = blocks_resource.read_index;
    BlockDescriptor *blockA = (BlockDescriptor *) blocks_resource.resource + read_index;
    read_index = (read_index + 1) % blocks_resource.size;
    BlockDescriptor *blockB = (BlockDescriptor *) blocks_resource.resource + read_index;
    read_index = (read_index + 1) % blocks_resource.size;
    blocks_resource.read_index = read_index;

    if (!(blockA->rx_id == 'A' && blockB->rx_id == 'B')) {
        fprintf(stderr, "mismatch rx_id - %c %c\n", blockA->rx_id, blockB->rx_id);
        streaming_status = STREAMING_STATUS_FAILED;
        return -1;
    }
    if (!(blockA->first_sample_num == blockB->first_sample_num)) {
        fprintf(stderr, "mismatch first_sample_num - %u %u\n", blockA->first_sample_num, blockB->first_sample_num);
        streaming_status = STREAMING_STATUS_FAILED;
        return -1;
    }
    if (!(blockA->num_samples == blockB->num_samples)) {
        fprintf(stderr, "mismatch num_samples - %u %u\n", blockA->num_samples, blockB->num_samples);
        streaming_status = STREAMING_STATUS_FAILED;
        return -1;
    }
    *pBlockA = blockA;
    *pBlockB = blockB;
    return 0;
}

static int write_buffer(const uint8_t *buf, size_t count) {
    struct timespec before_write_ts;
    struct timespec after_write_ts;
    while (count > 0) {
        clock_gettime(CLOCK_REALTIME, &before_write_ts);
        ssize_t nwritten = write(outputfd, buf, count);
        clock_gettime(CLOCK_REALTIME, &after_write_ts);
        stats.total_writes++;
        unsigned long long write_elapsed = (after_write_ts.tv_sec - before_write_ts.tv_sec) * 1000000000ULL + after_write_ts.tv_nsec - before_write_ts.tv_nsec;
        stats.total_write_elapsed += write_elapsed;
        if (write_elapsed > stats.max_write_elapsed) {
            stats.max_write_elapsed = write_elapsed;
        }
        if (nwritten == -1) {
            fprintf(stderr, "write samples failed: %s\n", strerror(errno));
            streaming_status = STREAMING_STATUS_FAILED;
            return -1;
        }
        if (nwritten == (ssize_t)count) {
            stats.full_writes++;
        } else if (nwritten == 0) {
            stats.zero_writes++;
        } else if (nwritten < (ssize_t)count) {
            stats.partial_writes++;
        }
        buf += nwritten;
        count -= nwritten;
        stats.data_size += nwritten;
    }
    return 0;
}

static void output_gain_changes() {
    pthread_mutex_lock(gain_changes_resource.lock);
    unsigned int nready = gain_changes_resource.nready;
    gain_changes_resource.nready = 0;
    pthread_mutex_unlock(gain_changes_resource.lock);
    unsigned int read_index = gain_changes_resource.read_index;
    unsigned int size = gain_changes_resource.size;
    GainChange *gain_changes = (GainChange *) gain_changes_resource.resource;
    unsigned int items_left = nready;
    while (items_left > 0) {
        unsigned int nitems = items_left <= size - read_index ? items_left : size - read_index;
        uint8_t *gaindata = (uint8_t *)(gain_changes + read_index);
        size_t bytes_left = nitems * sizeof(GainChange);
        while (bytes_left > 0) {
            ssize_t nwritten = write(gainsfd, gaindata, bytes_left);
            if (nwritten == -1) {
                fprintf(stderr, "write gains failed: %s\n", strerror(errno));
                streaming_status = STREAMING_STATUS_FAILED;
                goto gain_changes_finally;
            }
            gaindata += nwritten;
            bytes_left -= nwritten;
        }
        read_index = (read_index + nitems) % size;
        items_left -= nitems;
    }
    gain_changes_resource.read_index = read_index;

gain_changes_finally:
    pthread_mutex_lock(gain_changes_resource.lock);
    gain_changes_resource.nused -= nready;
    pthread_mutex_unlock(gain_changes_resource.lock);
}
