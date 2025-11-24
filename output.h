/* record to file the I/Q stream(s) from a SDRplay RSP
 * output functions
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _OUTPUT_H
#define _OUTPUT_H

/* global variables */
extern int outputfd;
extern int gainsfd;
extern short *outsamples;

/* public functions */
int output_open();
void output_close();
int output_validate_filename();

#endif /* _OUTPUT_H */
