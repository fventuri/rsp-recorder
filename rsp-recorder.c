/* record to file the I/Q stream(s) from a SDRplay RSP
 * main program
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "buffers.h"
#include "config.h"
#include "output.h"
#include "rsp-recorder.h"
#include "sdrplay-rsp.h"
#include "stats.h"
#include "streaming.h"

#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (get_config_from_cli(argc, argv) == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (sdrplay_rsp_open() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (sdrplay_select_rsp() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (sdrplay_validate_settings() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (sdrplay_configure_rsp() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (buffers_create() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (sdrplay_start_streaming() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (output_open() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (stream() == -1) {
        main_exit(EXIT_FAILURE);
    }
    if (print_stats() == -1) {
        main_exit(EXIT_FAILURE);
    }
    main_exit(EXIT_SUCCESS);
}

void main_exit(int exit_status)
{
    sdrplay_rsp_close();
    buffers_free();
    output_close();
    exit(exit_status);
}
