/* record to file the I/Q stream(s) from a SDRplay RSP
 * types and enums
 *
 * Copyright 2025 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef _TYPEDEFS_H
#define _TYPEDEFS_H

typedef enum {
    OUTPUT_TYPE_UNKNOWN,
    OUTPUT_TYPE_RAW,
    OUTPUT_TYPE_LINRAD,
    OUTPUT_TYPE_WAV,
} OutputType;

#endif /* _TYPEDEFS_H */
