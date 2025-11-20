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
int write_wav_header();
int finalize_wav_file();

#endif /* _WAV_H */
