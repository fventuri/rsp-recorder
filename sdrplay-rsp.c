/* record to file the I/Q stream(s) from a SDRplay RSP
 * SDRplay RSP related functions
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "callbacks.h"
#include "config.h"
#include "rsp-recorder.h"
#include "sdrplay-rsp.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <sdrplay_api.h>
#pragma GCC diagnostic pop

/* global variables */
bool is_dual_tuner = false;
int internal_decimation = 1;
double output_sample_rate = 0;

static sdrplay_api_DeviceT device;
static sdrplay_api_DeviceParamsT *device_params;

static bool is_sdrplay_api_open = false;
static bool is_sdrplay_api_locked = false;
static bool is_sdrplay_device_selected = false;
static bool is_sdrplay_rsp_streaming = false;

static CallbackContext callback_context;
static RXContext rx_context_A;
static RXContext rx_context_B;
static EventContext event_context;

/* internal functions */
static int sdrplay_internal_decimation(double fs, sdrplay_api_If_kHzT ifreq, sdrplay_api_Bw_MHzT bw);
static int sdrplay_select_antenna(sdrplay_api_DeviceParamsT *device_params);
static int sdrplay_select_notch_filter(sdrplay_api_DeviceParamsT *device_params);
static int sdrplay_enable_bias_t(sdrplay_api_DeviceParamsT *device_params);
static int sdrplay_select_rspdx_hdr_mode(sdrplay_api_DeviceParamsT *device_params);
static void sdrplay_print_settings();


int sdrplay_rsp_open()
{
    /* open SDRplay API and check version */
    sdrplay_api_ErrT err;
    err = sdrplay_api_Open();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Open() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    is_sdrplay_api_open = true;
    float ver;
    err = sdrplay_api_ApiVersion(&ver);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_ApiVersion() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    if (ver != SDRPLAY_API_VERSION) {
        fprintf(stderr, "SDRplay API version mismatch - expected=%.2f found=%.2f\n", SDRPLAY_API_VERSION, ver);
        return -1;
    }
    return 0;
}

void sdrplay_rsp_close()
{
    sdrplay_api_ErrT err;
    if (is_sdrplay_rsp_streaming) {
        err = sdrplay_api_Uninit(device.dev);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_Uninit() failed: %s\n", sdrplay_api_GetErrorString(err));
        }
        is_sdrplay_rsp_streaming = false;
    }
    if (is_sdrplay_device_selected) {
        err = sdrplay_api_ReleaseDevice(&device);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_ReleaseDevice() failed: %s\n", sdrplay_api_GetErrorString(err));
        }
        is_sdrplay_device_selected = false;
    }

    if (is_sdrplay_api_locked) {
        err = sdrplay_api_UnlockDeviceApi();
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_UnlockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        }
        is_sdrplay_api_locked = false;
    }

    if (is_sdrplay_api_open) {
        err = sdrplay_api_Close();
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_Close() failed: %s\n", sdrplay_api_GetErrorString(err));
        }
        is_sdrplay_api_open = false;
    }

    return;
}

int sdrplay_select_rsp()
{
    sdrplay_api_ErrT err;

    err = sdrplay_api_LockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_LockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    is_sdrplay_api_locked = true;
#ifdef SDRPLAY_MAX_DEVICES
#undef SDRPLAY_MAX_DEVICES
#endif
#define SDRPLAY_MAX_DEVICES 4
    unsigned int ndevices = SDRPLAY_MAX_DEVICES;
    sdrplay_api_DeviceT devices[SDRPLAY_MAX_DEVICES];
    err = sdrplay_api_GetDevices(devices, &ndevices, ndevices);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_GetDevices() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    int device_index = -1;
    for (unsigned int i = 0; i < ndevices; i++) {
        if (devices[i].valid) {
            if (serial_number == NULL || strcmp(devices[i].SerNo, serial_number) == 0) {
                device_index = i;
                break;
            }
        }
    }
    if (device_index == -1) {
        fprintf(stderr, "SDRplay RSP not found or not available\n");
        return -1;
    }
    device = devices[device_index];

    if (device.hwVer != SDRPLAY_RSPduo_ID) {
        if (!(rspduo_mode == sdrplay_api_RspDuoMode_Unknown || rspduo_mode == sdrplay_api_RspDuoMode_Single_Tuner)) {
            fprintf(stderr, "non RSPduo's only support single tuner mode\n");
            return -1;
        }
        rspduo_mode = sdrplay_api_RspDuoMode_Unknown;
    } else {
        /* select RSPduo mode */
        internal_decimation = sdrplay_internal_decimation(sample_rate, if_frequency, if_bandwidth);
        if (rspduo_mode == sdrplay_api_RspDuoMode_Unknown) {
            if (internal_decimation > 1) {
                if ((device.rspDuoMode & sdrplay_api_RspDuoMode_Dual_Tuner) == sdrplay_api_RspDuoMode_Dual_Tuner) {
                    rspduo_mode = sdrplay_api_RspDuoMode_Dual_Tuner;
                } else if ((device.rspDuoMode & sdrplay_api_RspDuoMode_Slave) == sdrplay_api_RspDuoMode_Slave) {
                    rspduo_mode = sdrplay_api_RspDuoMode_Slave;
                } else {
                    fprintf(stderr, "SDRplay RSPduo - no tuners available\n");
                    return -1;
                }
            } else {
                rspduo_mode = sdrplay_api_RspDuoMode_Single_Tuner;
            }
        } else {
            if (!(rspduo_mode == sdrplay_api_RspDuoMode_Single_Tuner || internal_decimation >1)) {
                fprintf(stderr, "SDRplay RSPduo dual tuner/master/slave modes are not supported with this set of (sample rate, IF frequency, IF bandwidth)\n");
                return -1;
            }
        }
        if (!((device.rspDuoMode & rspduo_mode) == rspduo_mode)) {
            fprintf(stderr, "SDRplay RSPduo mode not available\n");
            return -1;
        }
        sdrplay_api_TunerSelectT tuner;
        if (rspduo_mode == sdrplay_api_RspDuoMode_Dual_Tuner) {
            if (!(antenna == NULL || strcmp(antenna, "Both Tuners") == 0)) {
                fprintf(stderr, "Invalid RSPduo antenna selection: %s\n", antenna);
                return -1;
            }
            tuner = sdrplay_api_Tuner_Both;
        } else {
            if (antenna == NULL) {
                if (device.tuner & sdrplay_api_Tuner_A) {
                    tuner = sdrplay_api_Tuner_A;
                } else if (device.tuner & sdrplay_api_Tuner_B) {
                    tuner = sdrplay_api_Tuner_B;
                } else {
                    fprintf(stderr, "No RSPduo antenna available\n");
                    return -1;
                }
            } else if (strcmp(antenna, "Tuner 1 50 ohm") == 0) {
                tuner = sdrplay_api_Tuner_A;
            } else if (strcmp(antenna, "Tuner 2 50 ohm") == 0) {
                tuner = sdrplay_api_Tuner_B;
            } else if (strcmp(antenna, "High Z") == 0) {
                tuner = sdrplay_api_Tuner_A;
            } else {
                fprintf(stderr, "Invalid RSPduo antenna selection: %s\n", antenna);
                return -1;
            }
        }
        if (!((device.tuner & tuner) == tuner)) {
            fprintf(stderr, "SDRplay RSPduo tuner/antenna not available\n");
            return -1;
        }
        device.tuner = tuner;
        device.rspDuoMode = rspduo_mode;
        device.rspDuoSampleFreq = sample_rate;
        is_dual_tuner = rspduo_mode == sdrplay_api_RspDuoMode_Dual_Tuner;
    }

    err = sdrplay_api_SelectDevice(&device);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_SelectDevice() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    is_sdrplay_device_selected = true;

    err = sdrplay_api_UnlockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_UnlockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    is_sdrplay_api_locked = false;

    if (debug_enable) {
        err = sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Verbose);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_DebugEnable() failed: %s\n", sdrplay_api_GetErrorString(err));
            return -1;
        }
    }
    return 0;
}

int sdrplay_configure_rsp() {
    sdrplay_api_ErrT err = sdrplay_api_GetDeviceParams(device.dev, &device_params);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_GetDeviceParams() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    if (device_params->devParams != NULL) {
        device_params->devParams->fsFreq.fsHz = sample_rate;
        device_params->devParams->ppm = ppm;
    }
    if (rspduo_mode == sdrplay_api_RspDuoMode_Dual_Tuner) {
        sdrplay_api_RxChannelParamsT *rx_channelA_params = device_params->rxChannelA;
        sdrplay_api_RxChannelParamsT *rx_channelB_params = device_params->rxChannelB;
        rx_channelA_params->ctrlParams.decimation.enable = decimation > 1;
        rx_channelA_params->ctrlParams.decimation.decimationFactor = decimation;
        if (antenna != NULL && strcmp(antenna, "High Z") == 0) {
            rx_channelA_params->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_1;
        } else {
            rx_channelA_params->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
        }
        rx_channelA_params->tunerParams.ifType = if_frequency;
        rx_channelA_params->tunerParams.bwType = if_bandwidth;
        rx_channelA_params->ctrlParams.agc.enable = agc_A;
        if (agc_A == sdrplay_api_AGC_DISABLE) {
            rx_channelA_params->tunerParams.gain.gRdB = gRdB_A;
        }
        rx_channelA_params->tunerParams.gain.LNAstate = LNAstate_A;
        rx_channelA_params->rspDuoTunerParams.rfNotchEnable = RFNotch;
        rx_channelA_params->rspDuoTunerParams.rfDabNotchEnable = DABNotch;
        rx_channelA_params->rspDuoTunerParams.tuner1AmNotchEnable = rspDuoAMNotch;
        rx_channelA_params->ctrlParams.dcOffset.DCenable = DCenable;
        rx_channelA_params->ctrlParams.dcOffset.IQenable = IQenable;
        rx_channelB_params->ctrlParams.decimation.enable = decimation > 1;
        rx_channelB_params->ctrlParams.decimation.decimationFactor = decimation;
        rx_channelB_params->tunerParams.ifType = if_frequency;
        rx_channelB_params->tunerParams.bwType = if_bandwidth;
        rx_channelB_params->ctrlParams.agc.enable = agc_B;
        if (agc_B == sdrplay_api_AGC_DISABLE) {
            rx_channelB_params->tunerParams.gain.gRdB = gRdB_B;
        }
        rx_channelB_params->tunerParams.gain.LNAstate = LNAstate_B;
        rx_channelB_params->rspDuoTunerParams.rfNotchEnable = RFNotch;
        rx_channelB_params->rspDuoTunerParams.rfDabNotchEnable = DABNotch;
        rx_channelB_params->ctrlParams.dcOffset.DCenable = DCenable;
        rx_channelB_params->ctrlParams.dcOffset.IQenable = IQenable;
        rx_channelA_params->tunerParams.dcOffsetTuner.dcCal = dcCal;
        rx_channelA_params->tunerParams.dcOffsetTuner.speedUp = speedUp;
        rx_channelA_params->tunerParams.dcOffsetTuner.trackTime = trackTime;
        rx_channelA_params->tunerParams.dcOffsetTuner.refreshRateTime = refreshRateTime;
        rx_channelB_params->tunerParams.dcOffsetTuner.dcCal = dcCal;
        rx_channelB_params->tunerParams.dcOffsetTuner.speedUp = speedUp;
        rx_channelB_params->tunerParams.dcOffsetTuner.trackTime = trackTime;
        rx_channelB_params->tunerParams.dcOffsetTuner.refreshRateTime = refreshRateTime;
        rx_channelA_params->rspDuoTunerParams.biasTEnable = biasTEnable;
        rx_channelB_params->rspDuoTunerParams.biasTEnable = biasTEnable;
        rx_channelA_params->tunerParams.rfFreq.rfHz = frequency_A;
        rx_channelB_params->tunerParams.rfFreq.rfHz = frequency_B;
    } else if (rspduo_mode == sdrplay_api_RspDuoMode_Slave) {
        sdrplay_api_RxChannelParamsT *rx_channel_params;
        if (device.tuner == sdrplay_api_Tuner_A) {
            rx_channel_params = device_params->rxChannelA;
        } else if (device.tuner == sdrplay_api_Tuner_B) {
            rx_channel_params = device_params->rxChannelB;
        } else {
            fprintf(stderr, "SDRplay RSPduo in slave mode - invalid tuner: %d\n", device.tuner);
            return -1;
        }
        rx_channel_params->ctrlParams.agc.enable = agc_A;
        if (agc_A == sdrplay_api_AGC_DISABLE) {
            rx_channel_params->tunerParams.gain.gRdB = gRdB_A;
        }
        rx_channel_params->tunerParams.gain.LNAstate = LNAstate_A;
        rx_channel_params->rspDuoTunerParams.rfNotchEnable = RFNotch;
        rx_channel_params->rspDuoTunerParams.rfDabNotchEnable = DABNotch;
        rx_channel_params->rspDuoTunerParams.tuner1AmNotchEnable = rspDuoAMNotch;
        rx_channel_params->rspDuoTunerParams.biasTEnable = biasTEnable;
        rx_channel_params->tunerParams.rfFreq.rfHz = frequency_A;
    } else {
        /* single tuner mode for all RSP models (and master mode for RSPduo) */
        sdrplay_api_RxChannelParamsT *rx_channel_params;
        if (device.hwVer != SDRPLAY_RSPduo_ID) {
            rx_channel_params = device_params->rxChannelA;
        } else {
            if (device.tuner == sdrplay_api_Tuner_A) {
                rx_channel_params = device_params->rxChannelA;
            } else if (device.tuner == sdrplay_api_Tuner_B) {
                rx_channel_params = device_params->rxChannelB;
            } else {
                fprintf(stderr, "SDRplay RSPduo in single tuner or master mode - invalid tuner: %d\n", device.tuner);
                return -1;
            }
        }
        if (sdrplay_select_antenna(device_params) != 0) {
            return -1;
        }
        rx_channel_params->tunerParams.ifType = if_frequency;
        rx_channel_params->tunerParams.bwType = if_bandwidth;
        rx_channel_params->ctrlParams.agc.enable = agc_A;
        if (agc_A == sdrplay_api_AGC_DISABLE) {
            rx_channel_params->tunerParams.gain.gRdB = gRdB_A;
        }
        rx_channel_params->tunerParams.gain.LNAstate = LNAstate_A;
        if (sdrplay_select_notch_filter(device_params) != 0) {
            return -1;
        }
        rx_channel_params->ctrlParams.dcOffset.DCenable = DCenable;
        rx_channel_params->ctrlParams.dcOffset.IQenable = IQenable;
        rx_channel_params->tunerParams.dcOffsetTuner.dcCal = dcCal;
        rx_channel_params->tunerParams.dcOffsetTuner.speedUp = speedUp;
        rx_channel_params->tunerParams.dcOffsetTuner.trackTime = trackTime;
        rx_channel_params->tunerParams.dcOffsetTuner.refreshRateTime = refreshRateTime;
        if (biasTEnable) {
            if (sdrplay_enable_bias_t(device_params) != 0) {
                return -1;
            }
        }
        if (HDRmode) {
            if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
                if (sdrplay_select_rspdx_hdr_mode(device_params) != 0) {
                    return -1;
                }
            } else {
                fprintf(stderr, "HDR mode only supported with RSPdx or RSPdx-R2 models\n");
                return -1;
            }
        }
        rx_channel_params->tunerParams.rfFreq.rfHz = frequency_A;
    }

    internal_decimation = sdrplay_internal_decimation(sample_rate, if_frequency, if_bandwidth);
    output_sample_rate = sample_rate / internal_decimation / decimation;

    return 0;
}

static int sdrplay_internal_decimation(double fs, sdrplay_api_If_kHzT ifreq, sdrplay_api_Bw_MHzT bw) {
    typedef struct {
        double sample_rate;
        sdrplay_api_If_kHzT if_frequency;
        sdrplay_api_Bw_MHzT if_bandwidth;
        int decimation;
    } InternalDecimation;

    InternalDecimation internal_decimations[] = {
        {8.192e6, sdrplay_api_IF_2_048, sdrplay_api_BW_1_536, 4},
        {8e6,     sdrplay_api_IF_2_048, sdrplay_api_BW_1_536, 4},
        {8e6,     sdrplay_api_IF_2_048, sdrplay_api_BW_5_000, 4},
        {2e6,     sdrplay_api_IF_0_450, sdrplay_api_BW_0_200, 4},
        {2e6,     sdrplay_api_IF_0_450, sdrplay_api_BW_0_300, 4},
        {2e6,     sdrplay_api_IF_0_450, sdrplay_api_BW_0_600, 2},
        {6e6,     sdrplay_api_IF_1_620, sdrplay_api_BW_0_200, 3},
        {6e6,     sdrplay_api_IF_1_620, sdrplay_api_BW_0_300, 3},
        {6e6,     sdrplay_api_IF_1_620, sdrplay_api_BW_0_600, 3},
        {6e6,     sdrplay_api_IF_1_620, sdrplay_api_BW_1_536, 3}
    };

    int n = sizeof(internal_decimations) / sizeof(internal_decimations[0]);
    for (int i = 0; i < n; i++) {
        if (fs == internal_decimations[i].sample_rate &&
            ifreq == internal_decimations[i].if_frequency &&
            bw == internal_decimations[i].if_bandwidth) {
            return internal_decimations[i].decimation;
        }
    }
    return 1;
}

static int sdrplay_select_antenna(sdrplay_api_DeviceParamsT *device_params) {
    if (device.hwVer == SDRPLAY_RSP1_ID || device.hwVer == SDRPLAY_RSP1A_ID || SDRPLAY_RSP1B_ID) {
        if (antenna != NULL) {
            fprintf(stderr, "No antenna selection for this RSP model\n");
            return -1;
        }
    } else if (device.hwVer == SDRPLAY_RSP2_ID) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        if (antenna == NULL || strcmp(antenna, "Antenna A") == 0) {
            rx_channel_params->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_A;
            rx_channel_params->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_2;
        } else if (strcmp(antenna, "Antenna B") == 0) {
            rx_channel_params->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_B;
            rx_channel_params->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_2;
        } else if (strcmp(antenna, "Hi-Z") == 0) {
            rx_channel_params->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_A;
            rx_channel_params->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_1;
        } else {
            fprintf(stderr, "Invalid RSP2 antenna selection: %s\n", antenna);
            return -1;
        }
    } else if (device.hwVer == SDRPLAY_RSPduo_ID) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        if (strcmp(antenna, "High Z") == 0) {
            rx_channel_params->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_1;
        } else {
            rx_channel_params->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
        }
    } else if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
        if (antenna == NULL || strcmp(antenna, "Antenna A") == 0) {
            device_params->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_A;
        } else if (strcmp(antenna, "Antenna B") == 0) {
            device_params->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_B;
        } else if (strcmp(antenna, "Antenna C") == 0) {
            device_params->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_C;
        } else {
            fprintf(stderr, "Invalid RSPdx/RSPdx-R2 antenna selection: %s\n", antenna);
            return -1;
        }
    }

    return 0;
}

static int sdrplay_select_notch_filter(sdrplay_api_DeviceParamsT *device_params) {
    if (device.hwVer == SDRPLAY_RSP1_ID) {
        if (RFNotch || DABNotch || rspDuoAMNotch) {
           fprintf(stderr, "No notch filters for this RSP model\n");
           return -1;
       }
    } else if (device.hwVer == SDRPLAY_RSP1A_ID || SDRPLAY_RSP1B_ID) {
        device_params->devParams->rsp1aParams.rfNotchEnable = RFNotch;
        device_params->devParams->rsp1aParams.rfDabNotchEnable = DABNotch;
        if (rspDuoAMNotch) {
            fprintf(stderr, "No RSPduo notch filters for this RSP model\n");
            return -1;
        }
    } else if (device.hwVer == SDRPLAY_RSP2_ID) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        rx_channel_params->rsp2TunerParams.rfNotchEnable = RFNotch;
        if (DABNotch || rspDuoAMNotch) {
            fprintf(stderr, "No DAB or RSPduo notch filters for this RSP model\n");
            return -1;
        }
    } else if (device.hwVer == SDRPLAY_RSPduo_ID) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        rx_channel_params->rspDuoTunerParams.rfNotchEnable = RFNotch;
        rx_channel_params->rspDuoTunerParams.rfDabNotchEnable = DABNotch;
        rx_channel_params->rspDuoTunerParams.tuner1AmNotchEnable = rspDuoAMNotch;
    } else if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
        device_params->devParams->rspDxParams.rfNotchEnable = RFNotch;
        device_params->devParams->rspDxParams.rfDabNotchEnable = DABNotch;
        if (rspDuoAMNotch) {
            fprintf(stderr, "No RSPduo notch filters for this RSP model\n");
            return -1;
        }
    }

    return 0;
}

static int sdrplay_enable_bias_t(sdrplay_api_DeviceParamsT *device_params) {
    if (device.hwVer == SDRPLAY_RSP1_ID) {
        fprintf(stderr, "Bias-T not supported for this RSP model\n");
        return -1;
    } else if (device.hwVer == SDRPLAY_RSP1A_ID || SDRPLAY_RSP1B_ID) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        rx_channel_params->rsp1aTunerParams.biasTEnable = biasTEnable;
    } else if (device.hwVer == SDRPLAY_RSP2_ID) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        rx_channel_params->rsp2TunerParams.biasTEnable = biasTEnable;
    } else if (device.hwVer == SDRPLAY_RSPduo_ID) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        rx_channel_params->rspDuoTunerParams.biasTEnable = biasTEnable;
    } else if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
        device_params->devParams->rspDxParams.biasTEnable = biasTEnable;
    }

    return 0;
}

static int sdrplay_select_rspdx_hdr_mode(sdrplay_api_DeviceParamsT *device_params) {
    double hdr_mode_frequencies[] = { 135e3, 175e3, 220e3, 250e3, 340e3, 475e3, 516e3, 875e3, 1125e3, 1900e3 };
    int n = sizeof(hdr_mode_frequencies) / sizeof(hdr_mode_frequencies[0]);
    bool ok = false;
    for (int i = 0; i < n; i++) {
        if (frequency_A == hdr_mode_frequencies[i]) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        fprintf(stderr, "HDR mode only works with one of these frequencies:");
        fprintf(stderr, " %.0fkHz", hdr_mode_frequencies[0] / 1e3);
        for (int i = 1; i < n; i++) {
            fprintf(stderr, ", %.0fkHz", hdr_mode_frequencies[i] / 1e3);
        }
        fprintf(stderr, "\n");
        return -1;
    }
    if (sample_rate != 6e6) {
        fprintf(stderr, "HDR mode only works with sample rate = 6MHz\n");
        return -1;
    }
    if (if_frequency != sdrplay_api_IF_1_620) {
        fprintf(stderr, "HDR mode only works with IF frequency = 1620kHz\n");
        return -1;
    }
    device_params->devParams->rspDxParams.hdrEnable = HDRmode;
    device_params->rxChannelA->rspDxTunerParams.hdrBw = hdr_mode_bandwidth;
    return 0;
}

int sdrplay_validate_settings()
{
    if (rspduo_mode == sdrplay_api_RspDuoMode_Unknown || rspduo_mode == sdrplay_api_RspDuoMode_Single_Tuner) {
        if (agc_A != agc_B) {
            fprintf(stderr, "only one AGC value allowed in single tuner (or master/slave) mode\n");
            return -1;
        }
        if (gRdB_A != gRdB_B) {
            fprintf(stderr, "only one IF gain reduction value allowed in single tuner (or master/slave) mode\n");
            return -1;
        }
        if (LNAstate_A != LNAstate_B) {
            fprintf(stderr, "only one LNA state allowed in single tuner (or master/slave) mode\n");
            return -1;
        }
        if (frequency_A != frequency_B) {
            fprintf(stderr, "only one frequency allowed in single tuner (or master/slave) mode\n");
            return -1;
        }
    }
    return 0;
}

int sdrplay_start_streaming() {
    // callbacks and callback contexts
    sdrplay_api_CallbackFnsT callbackFns;

    if (!is_dual_tuner) {
        // single tuner case
        rx_context_A = (RXContext) {
            .next_sample_num = 0xffffffff,
            .internal_decimation = internal_decimation,
            .blocks_resource = &blocks_resource,
            .samples_resource = &samples_resource,
            .timeinfo = &timeinfo,
            .rx_stats = &rx_stats_A,
        };
    
        event_context = (EventContext) {
            .gain_changes_resource = gains_file_enable ? &gain_changes_resource : NULL,
            .total_samples = {
                &rx_stats_A.total_samples,
                NULL,
            },
        };
    
        callback_context = (CallbackContext) {
            .rx_contexts = {
                &rx_context_A,
                NULL,
            },
            .event_context = &event_context,
        };
    
        callbackFns = (sdrplay_api_CallbackFnsT) {
            rxA_callback,
            NULL,
            event_callback,
        };

    } else {
        // dual tuner case
        rx_context_A = (RXContext) {
            .next_sample_num = 0xffffffff,
            .internal_decimation = internal_decimation,
            .blocks_resource = &blocks_resource,
            .samples_resource = &samples_resource,
            .timeinfo = &timeinfo,
            .rx_stats = &rx_stats_A,
        };
        rx_context_B = (RXContext) {
            .next_sample_num = 0xffffffff,
            .internal_decimation = internal_decimation,
            .blocks_resource = &blocks_resource,
            .samples_resource = &samples_resource,
            .timeinfo = NULL,
            .rx_stats = &rx_stats_B,
        };
    
        event_context = (EventContext) {
            .gain_changes_resource = gains_file_enable ? &gain_changes_resource : NULL,
            .total_samples = {
                &rx_stats_A.total_samples,
                &rx_stats_B.total_samples,
            },
        };
    
        callback_context = (CallbackContext) {
            .rx_contexts = {
                &rx_context_A,
                &rx_context_B,
            },
            .event_context = &event_context,
        };
    
        callbackFns = (sdrplay_api_CallbackFnsT) {
            rxA_callback,
            rxB_callback,
            event_callback,
        };
    }

    /* many thanks to @bminish for suggesting bulk mode! */
    if (device_params->devParams != NULL) {
        device_params->devParams->mode = sdrplay_api_BULK;
    }

    sdrplay_api_ErrT err;
    err = sdrplay_api_Init(device.dev, &callbackFns, &callback_context);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        return -1;
    }
    is_sdrplay_rsp_streaming = true;

    // since sdrplay_api_Init() resets channelB settings to channelA values,
    // we need to update all the settings for channelB that are different
    if (rspduo_mode == sdrplay_api_RspDuoMode_Dual_Tuner) {
        sdrplay_api_RxChannelParamsT *rx_channelB_params = device_params->rxChannelB;
        sdrplay_api_ReasonForUpdateT reason_for_update = sdrplay_api_Update_None;
        if (agc_B != agc_A) {
            rx_channelB_params->ctrlParams.agc.enable = agc_B;
            reason_for_update |= sdrplay_api_Update_Ctrl_Agc;
        }
        if (agc_B == sdrplay_api_AGC_DISABLE) {
            if (gRdB_B != gRdB_A) {
                rx_channelB_params->tunerParams.gain.gRdB = gRdB_B;
                reason_for_update |= sdrplay_api_Update_Tuner_Gr;
            }
        }
        if (LNAstate_B != LNAstate_A) {
            rx_channelB_params->tunerParams.gain.LNAstate = LNAstate_B;
            reason_for_update |= sdrplay_api_Update_Tuner_Gr;
        }
        if (frequency_B != frequency_A) {
            rx_channelB_params->tunerParams.rfFreq.rfHz = frequency_B;
            reason_for_update |= sdrplay_api_Update_Tuner_Frf;
        }
        if (reason_for_update != sdrplay_api_Update_None) {
            err = sdrplay_api_Update(device.dev, sdrplay_api_Tuner_B, reason_for_update, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success) {
                fprintf(stderr, "sdrplay_api_Update(0x%08x) failed: %s\n", reason_for_update, sdrplay_api_GetErrorString(err));
                return -1;
            }
        }
    }

    if (verbose) {
        sdrplay_print_settings();
    }
    return 0;
}

void sdrplay_acknowledge_power_overload(sdrplay_api_TunerSelectT tuner) {
    sdrplay_api_Update(device.dev, tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck, sdrplay_api_Update_Ext1_None);
}

float sdrplay_get_current_gain(int tuner) {
    switch (tuner) {
    case 0:
        return device_params->rxChannelA->tunerParams.gain.gainVals.curr;
    case 1:
        return device_params->rxChannelB->tunerParams.gain.gainVals.curr;
    }
    return 0;
}

unsigned long long estimate_data_size() {
    unsigned int nrx = is_dual_tuner ? 2 : 1;
    return (unsigned long long) output_sample_rate * nrx * 2 * sizeof(short) * streaming_time;
}

static void sdrplay_print_settings() {
    if (!is_dual_tuner) {
        sdrplay_api_RxChannelParamsT *rx_channel_params = device_params->rxChannelA;
        if (device.hwVer == SDRPLAY_RSPduo_ID) {
            if (device.tuner == sdrplay_api_Tuner_A) {
                rx_channel_params = device_params->rxChannelA;
            } else if (device.tuner == sdrplay_api_Tuner_B) {
                rx_channel_params = device_params->rxChannelB;
            }
        }
        if (device_params->devParams != NULL) {
            fprintf(stderr, "SerNo=%s hwVer=%d tuner=0x%02x rspSampleFreq=%.0lf ppm=%lf internalDecimation=%d\n", device.SerNo, device.hwVer, device.tuner, device_params->devParams->fsFreq.fsHz, device_params->devParams->ppm, internal_decimation);
        } else {
            fprintf(stderr, "SerNo=%s hwVer=%d tuner=0x%02x internalDecimation=%d\n", device.SerNo, device.hwVer, device.tuner, internal_decimation);
        }
        fprintf(stderr, "RX tuner - LO=%.0lf BW=%d IF=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channel_params->tunerParams.rfFreq.rfHz, rx_channel_params->tunerParams.bwType, rx_channel_params->tunerParams.ifType, rx_channel_params->ctrlParams.decimation.decimationFactor, rx_channel_params->ctrlParams.agc.enable, rx_channel_params->tunerParams.gain.gRdB, rx_channel_params->tunerParams.gain.LNAstate);
        fprintf(stderr, "RX tuner - DCenable=%d IQenable=%d dcCal=%d speedUp=%d trackTime=%d refreshRateTime=%d\n", (int)(rx_channel_params->ctrlParams.dcOffset.DCenable), (int)(rx_channel_params->ctrlParams.dcOffset.IQenable), (int)(rx_channel_params->tunerParams.dcOffsetTuner.dcCal), (int)(rx_channel_params->tunerParams.dcOffsetTuner.speedUp), rx_channel_params->tunerParams.dcOffsetTuner.trackTime, rx_channel_params->tunerParams.dcOffsetTuner.refreshRateTime);
        /* RSP model specific settings */
        if (device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID) {
            fprintf(stderr, "RSP1A/RSP1B specific - rfNotchEnable=%d rfDabNotchEnable=%d biasTEnable=%d\n", (int)(device_params->devParams->rsp1aParams.rfNotchEnable), (int)(device_params->devParams->rsp1aParams.rfDabNotchEnable), (int)(rx_channel_params->rsp1aTunerParams.biasTEnable));
        } else if (device.hwVer == SDRPLAY_RSP2_ID) {
            fprintf(stderr, "RSP2 specific - antennaSel=%d amPortSel=%d rfNotchEnable=%d biasTEnable=%d\n", (int)(rx_channel_params->rsp2TunerParams.antennaSel), (int)(rx_channel_params->rsp2TunerParams.amPortSel), (int)(rx_channel_params->rsp2TunerParams.rfNotchEnable), (int)(rx_channel_params->rsp2TunerParams.biasTEnable));
        } else if (device.hwVer == SDRPLAY_RSPduo_ID) {
            fprintf(stderr, "RSPduo specific - rspDuoMode=0x%02x rspDuoSampleFreq=%.0lf tuner1AmPortSel=%d rfNotchEnable=%d rfDabNotchEnable=%d tuner1AmNotchEnable=%d biasTEnable=%d\n", device.rspDuoMode, device.rspDuoSampleFreq, (int)(rx_channel_params->rspDuoTunerParams.tuner1AmPortSel), (int)(rx_channel_params->rspDuoTunerParams.rfNotchEnable), (int)(rx_channel_params->rspDuoTunerParams.rfDabNotchEnable), (int)(rx_channel_params->rspDuoTunerParams.tuner1AmNotchEnable), (int)(rx_channel_params->rspDuoTunerParams.biasTEnable));
        } else if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
            fprintf(stderr, "RSPdx/RSPdx-R2 specific - antennaSel=%d rfNotchEnable=%d rfDabNotchEnable=%d biasTEnable=%d hdrEnable=%d hdrBw=%d\n", (int)(device_params->devParams->rspDxParams.antennaSel), (int)(device_params->devParams->rspDxParams.rfNotchEnable), (int)(device_params->devParams->rspDxParams.rfDabNotchEnable), (int)(device_params->devParams->rspDxParams.biasTEnable), (int)(device_params->devParams->rspDxParams.hdrEnable), (int)(rx_channel_params->rspDxTunerParams.hdrBw));
        }
    } else {
        sdrplay_api_RxChannelParamsT *rx_channelA_params = device_params->rxChannelA;
        sdrplay_api_RxChannelParamsT *rx_channelB_params = device_params->rxChannelB;
        fprintf(stderr, "SerNo=%s hwVer=%d tuner=0x%02x rspDuoMode=0x%02x rspDuoSampleFreq=%.0lf internalDecimation=%d\n", device.SerNo, device.hwVer, device.tuner, device.rspDuoMode, device.rspDuoSampleFreq, internal_decimation);
        fprintf(stderr, "RX A - LO=%.0lf BW=%d IF=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channelA_params->tunerParams.rfFreq.rfHz, rx_channelA_params->tunerParams.bwType, rx_channelA_params->tunerParams.ifType, rx_channelA_params->ctrlParams.decimation.decimationFactor, rx_channelA_params->ctrlParams.agc.enable, rx_channelA_params->tunerParams.gain.gRdB, rx_channelA_params->tunerParams.gain.LNAstate);
        fprintf(stderr, "RX A - DCenable=%d IQenable=%d dcCal=%d speedUp=%d trackTime=%d refreshRateTime=%d\n", (int)(rx_channelA_params->ctrlParams.dcOffset.DCenable), (int)(rx_channelA_params->ctrlParams.dcOffset.IQenable), (int)(rx_channelA_params->tunerParams.dcOffsetTuner.dcCal), (int)(rx_channelA_params->tunerParams.dcOffsetTuner.speedUp), rx_channelA_params->tunerParams.dcOffsetTuner.trackTime, rx_channelA_params->tunerParams.dcOffsetTuner.refreshRateTime);
        fprintf(stderr, "RX A - tuner1AmPortSel=%d rfNotchEnable=%d rfDabNotchEnable=%d tuner1AmNotchEnable=%d biasTEnable=%d\n", (int)(rx_channelA_params->rspDuoTunerParams.tuner1AmPortSel), (int)(rx_channelA_params->rspDuoTunerParams.rfNotchEnable), (int)(rx_channelA_params->rspDuoTunerParams.rfDabNotchEnable), (int)(rx_channelA_params->rspDuoTunerParams.tuner1AmNotchEnable), (int)(rx_channelA_params->rspDuoTunerParams.biasTEnable));
        fprintf(stderr, "RX B - LO=%.0lf BW=%d IF=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channelB_params->tunerParams.rfFreq.rfHz, rx_channelB_params->tunerParams.bwType, rx_channelB_params->tunerParams.ifType, rx_channelB_params->ctrlParams.decimation.decimationFactor, rx_channelB_params->ctrlParams.agc.enable, rx_channelB_params->tunerParams.gain.gRdB, rx_channelB_params->tunerParams.gain.LNAstate);
        fprintf(stderr, "RX B - DCenable=%d IQenable=%d dcCal=%d speedUp=%d trackTime=%d refreshRateTime=%d\n", (int)(rx_channelB_params->ctrlParams.dcOffset.DCenable), (int)(rx_channelB_params->ctrlParams.dcOffset.IQenable), (int)(rx_channelB_params->tunerParams.dcOffsetTuner.dcCal), (int)(rx_channelB_params->tunerParams.dcOffsetTuner.speedUp), rx_channelB_params->tunerParams.dcOffsetTuner.trackTime, rx_channelB_params->tunerParams.dcOffsetTuner.refreshRateTime);
        fprintf(stderr, "RX B - tuner1AmPortSel=%d rfNotchEnable=%d rfDabNotchEnable=%d tuner1AmNotchEnable=%d biasTEnable=%d\n", (int)(rx_channelB_params->rspDuoTunerParams.tuner1AmPortSel), (int)(rx_channelB_params->rspDuoTunerParams.rfNotchEnable), (int)(rx_channelB_params->rspDuoTunerParams.rfDabNotchEnable), (int)(rx_channelB_params->rspDuoTunerParams.tuner1AmNotchEnable), (int)(rx_channelB_params->rspDuoTunerParams.biasTEnable));
    }
}
