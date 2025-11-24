/* record to file the I/Q stream(s) from a SDRplay RSP
 * RIFF/RF64/WAVE format
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _WAV_H
#define _WAV_H

/* public functions */
int write_sdruno_header();
int write_sdrconnect_header();
int write_experimental_header();
int finalize_sdruno_file();
int finalize_sdrconnect_file();
int finalize_experimental_file();

#endif /* _WAV_H */
