/* record to file the I/Q stream(s) from a SDRplay RSP
 * icallback functions
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "callbacks.h"
#include "sdrplay-rsp.h"
#include "streaming.h"

#define UNUSED(x) (void)(x)

/* global variables */
unsigned long long num_gain_changes[2] = {0L, 0L};

static unsigned int firstSampleNum = 0;

/* internal functions */
static void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, RXContext *rxContext, char rx_id, StreamingStatus streaming_status_rx_callback);
static void update_timeinfo(TimeInfo *timeinfo, unsigned long long sample_num, StreamingStatus streaming_status_rx_callback);
static int write_samples_to_circular_buffer(unsigned int num_samples, unsigned int first_sample_num, const short *xi, const short *xq, RXContext *rx_context, char rx_id);


void rxA_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    StreamingStatus streaming_status_rx_callback = streaming_status;
    firstSampleNum = params->firstSampleNum;
    RXContext *rx_context = ((CallbackContext *)cbContext)->rx_contexts[0];
    update_timeinfo(rx_context->timeinfo, rx_context->rx_stats->total_samples, streaming_status_rx_callback);
    rx_callback(xi, xq, params, numSamples, reset, rx_context, 'A', streaming_status_rx_callback);
}

void rxB_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    StreamingStatus streaming_status_rx_callback = streaming_status;
    if (params->firstSampleNum != firstSampleNum) {
        fprintf(stderr, "firstSampleNum mismatch - RXA=%d RXB=%d\n", firstSampleNum, params->firstSampleNum);
    }
    RXContext *rx_context = ((CallbackContext *)cbContext)->rx_contexts[1];
    rx_callback(xi, xq, params, numSamples, reset, rx_context, 'B', streaming_status_rx_callback);
}

void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext)
{
    if (eventId == sdrplay_api_GainChange &&
        (streaming_status == STREAMING_STATUS_STARTING ||
         streaming_status == STREAMING_STATUS_RUNNING ||
         streaming_status == STREAMING_STATUS_TERMINATE)) {
        EventContext *eventContext = ((CallbackContext *)cbContext)->event_context;
        int tuner_index = 0;
        if (is_dual_tuner) {
            tuner_index = tuner - 1;
        }
        uint64_t sample_num = streaming_status == STREAMING_STATUS_STARTING ? 0 : *eventContext->total_samples[tuner_index];
        num_gain_changes[tuner_index]++;
        ResourceDescriptor *gain_changes_resource = eventContext->gain_changes_resource;
        if (gain_changes_resource != NULL) {
            pthread_mutex_lock(gain_changes_resource->lock);
            unsigned int gain_changes_write_index = gain_changes_resource->write_index;
            bool gain_changes_has_enough_space = gain_changes_resource->nused < gain_changes_resource->size;
            if (gain_changes_has_enough_space) {
                gain_changes_resource->write_index = (gain_changes_write_index + 1) % gain_changes_resource->size;
                gain_changes_resource->nused++;
            }
            pthread_mutex_unlock(gain_changes_resource->lock);
            if (!gain_changes_has_enough_space) {
                fprintf(stderr, "gain changes buffer full\n");
                streaming_status = STREAMING_STATUS_GAIN_CHANGES_BUFFER_FULL;
                return;
            }
            GainChange *gain_change = (GainChange *)gain_changes_resource->resource + gain_changes_write_index;
            sdrplay_api_GainCbParamT *gain_params = (sdrplay_api_GainCbParamT *)params;
            gain_change->sample_num = sample_num;
            gain_change->currGain = gain_params->currGain;
            gain_change->tuner = tuner_index;
            gain_change->gRdB = gain_params->gRdB;
            gain_change->lnaGRdB = gain_params->lnaGRdB;

            pthread_mutex_lock(gain_changes_resource->lock);
            gain_changes_resource->nready++;
            pthread_mutex_unlock(gain_changes_resource->lock);
        }
    }
    return;
}

/* internal functions*/
static void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, RXContext *rxContext, char rx_id, StreamingStatus streaming_status_rx_callback)
{
    UNUSED(reset);

    if (streaming_status_rx_callback == STREAMING_STATUS_TERMINATE) {
        /* just return a block with num_samples set to 0
         * to signal the end of streaming
         */
        if (write_samples_to_circular_buffer(0, params->firstSampleNum, NULL, NULL, rxContext, rx_id) == -1) {
            streaming_status_rx_callback = streaming_status;
            return;
        }
        return;
    }

    /* only process samples when streaming status is set to RUNNING */
    if (streaming_status_rx_callback != STREAMING_STATUS_RUNNING) {
        return;
    }

    RXStats *rxStats = rxContext->rx_stats;

    /* track callback timestamp */
    clock_gettime(CLOCK_REALTIME, &rxStats->latest_callback);
    if (rxStats->earliest_callback.tv_sec == 0) {
        rxStats->earliest_callback.tv_sec = rxStats->latest_callback.tv_sec;
        rxStats->earliest_callback.tv_nsec = rxStats->latest_callback.tv_nsec;
    }
    rxStats->total_samples += numSamples;

    /* check for dropped samples */
    unsigned int dropped_samples;
    if (rxContext->next_sample_num != 0xffffffff && params->firstSampleNum != rxContext->next_sample_num) {
        if (rxContext->next_sample_num < params->firstSampleNum) {
            dropped_samples = params->firstSampleNum - rxContext->next_sample_num;
        } else {
            dropped_samples = UINT_MAX - (params->firstSampleNum - rxContext->next_sample_num) + 1;
        }
        rxStats->dropped_samples += dropped_samples;
        // fv
        //fprintf(stderr, "RX %c - dropped %d samples\n", rx_id, dropped_samples);
    }
    unsigned int nsntmp = (params->firstSampleNum + numSamples) * rxContext->internal_decimation;
    rxContext->next_sample_num = (nsntmp + (nsntmp % 4 < 2)) / rxContext->internal_decimation;

    /* update rx stats */
    rxStats->num_samples_min = rxStats->num_samples_min < numSamples ? rxStats->num_samples_min : numSamples;
    rxStats->num_samples_max = rxStats->num_samples_max > numSamples ? rxStats->num_samples_max : numSamples;

    short imin = SHRT_MAX;
    short imax = SHRT_MIN;
    short qmin = SHRT_MAX;
    short qmax = SHRT_MIN;
    for (unsigned int i = 0; i < numSamples; i++) {
        imin = imin < xi[i] ? imin : xi[i];
        imax = imax > xi[i] ? imax : xi[i];
    }
    for (unsigned int i = 0; i < numSamples; i++) {
        qmin = qmin < xq[i] ? qmin : xq[i];
        qmax = qmax > xq[i] ? qmax : xq[i];
    }
    rxStats->imin = rxStats->imin < imin ? rxStats->imin : imin;
    rxStats->imax = rxStats->imax > imax ? rxStats->imax : imax;
    rxStats->qmin = rxStats->qmin < qmin ? rxStats->qmin : qmin;
    rxStats->qmax = rxStats->qmax > qmax ? rxStats->qmax : qmax;

    if (write_samples_to_circular_buffer(numSamples, params->firstSampleNum, xi, xq, rxContext, rx_id) == -1) {
        streaming_status_rx_callback = streaming_status;
        return;
    }
}

static void update_timeinfo(TimeInfo *timeinfo, unsigned long long sample_num, StreamingStatus streaming_status_rx_callback) {
    if (streaming_status_rx_callback == STREAMING_STATUS_RUNNING) {
        if (timeinfo->start_ts.tv_sec == 0) {
            clock_gettime(CLOCK_REALTIME, &timeinfo->start_ts);
        }
        if (timeinfo->markers != NULL) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            time_t timetick_curr = ts.tv_sec / timeinfo->marker_interval;
            if (timetick_curr > timeinfo->timetick_curr) {
                if (timeinfo->markers_curr_idx < timeinfo->markers_max_idx) {
                    TimeMarker *tm = &timeinfo->markers[timeinfo->markers_curr_idx];
                    tm->ts.tv_sec = ts.tv_sec;
                    tm->ts.tv_nsec = ts.tv_nsec;
                    tm->sample_num = sample_num;
                    timeinfo->markers_curr_idx++;
                }
                timeinfo->timetick_curr = timetick_curr;
            }
        }
    } else if (streaming_status_rx_callback == STREAMING_STATUS_TERMINATE || streaming_status_rx_callback == STREAMING_STATUS_DONE) {
        if (timeinfo->stop_ts.tv_sec == 0) {
            clock_gettime(CLOCK_REALTIME, &timeinfo->stop_ts);
        }
    }
}

static int write_samples_to_circular_buffer(unsigned int num_samples,
    unsigned int first_sample_num, const short *xi, const short *xq,
    RXContext *rx_context, char rx_id) {

    ResourceDescriptor *samples_resource = NULL;
    unsigned int samples_write_index = 0;
    if (num_samples > 0) {
        /* prepare to write samples to circular buffer */
        samples_resource = rx_context->samples_resource;

        unsigned int samples_space_required = 2 * num_samples;
        unsigned int samples_nused_max = samples_resource->size - samples_space_required;
        pthread_mutex_lock(samples_resource->lock);
        samples_write_index = samples_resource->write_index;
        if (samples_write_index > samples_nused_max) {
            samples_nused_max -= samples_resource->size - samples_write_index;
            samples_write_index = 0;
        }
        bool samples_has_enough_space = samples_resource->nused < samples_nused_max;
        if (samples_has_enough_space) {
            samples_resource->write_index = samples_write_index + samples_space_required;
            samples_resource->nused += samples_space_required;
            if (samples_resource->nused > samples_resource->nused_max) {
                samples_resource->nused_max = samples_resource->nused;
            }
        }
        pthread_mutex_unlock(samples_resource->lock);
        if (!samples_has_enough_space) {
            fprintf(stderr, "samples buffer full\n");
            streaming_status = STREAMING_STATUS_SAMPLES_BUFFER_FULL;
            return -1;
        }
    }

    ResourceDescriptor *blocks_resource = rx_context->blocks_resource;
    pthread_mutex_lock(blocks_resource->lock);
    unsigned int blocks_write_index = blocks_resource->write_index;
    bool blocks_has_enough_space = blocks_resource->nused < blocks_resource->size;
    if (blocks_has_enough_space) {
        blocks_resource->write_index = (blocks_resource->write_index + 1) % blocks_resource->size;
        blocks_resource->nused++;
        if (blocks_resource->nused > blocks_resource->nused_max) {
            blocks_resource->nused_max = blocks_resource->nused;
        }
    }
    pthread_mutex_unlock(blocks_resource->lock);
    if (!blocks_has_enough_space) {
        fprintf(stderr, "blocks buffer full\n");
        streaming_status = STREAMING_STATUS_BLOCKS_BUFFER_FULL;
        return -1;
    }

    if (num_samples > 0) {
        /* fill the samples buffer */
        short *samples = (short *)samples_resource->resource + samples_write_index;
        memcpy(samples, xi, num_samples * sizeof(short));
        samples += num_samples;
        memcpy(samples, xq, num_samples * sizeof(short));
    }

    /* fill the block */
    BlockDescriptor *block = (BlockDescriptor *)blocks_resource->resource + blocks_write_index;
    block->first_sample_num = first_sample_num;
    block->num_samples = num_samples;
    block->samples_index = samples_write_index;
    block->rx_id = rx_id;

    /* all done; let the writer thread know there's data ready */
    pthread_mutex_lock(blocks_resource->lock);
    blocks_resource->nready++;
    pthread_cond_signal(blocks_resource->is_ready);
    pthread_mutex_unlock(blocks_resource->lock);

    return 0;
}
