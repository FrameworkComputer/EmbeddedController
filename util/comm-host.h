/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef COMM_HOST_H
#define COMM_HOST_H

#include "common.h"
#include "ec_commands.h"

/* Perform initializations needed for subsequent requests
 *
 * returns 0 in case of success or error code. */
int comm_init(void);

/*
 * Send a command to the EC.  Returns the length of output data returned (0 if
 * none), or a negative number if error; errors are -EC_RES_* constants from
 * ec_commands.h.
 */
int ec_command(int command, int version, const void *indata, int insize,
	       void *outdata, int outsize);

/*
 * Return the content of the EC information area mapped as "memory".
 * The offsets are defined by the EC_MEMMAP_ constants.
 */
uint8_t read_mapped_mem8(uint8_t offset);
uint16_t read_mapped_mem16(uint8_t offset);
uint32_t read_mapped_mem32(uint8_t offset);
/*
 * Read a memory-mapped string at the specified offset and store into buf,
 * which must be at least size EC_MEMMAP_TEXT_MAX.  Returns the length of
 * the copied string, not counting the terminating '\0', or <0 if error.
 */
int read_mapped_string(uint8_t offset, char *buf);

#endif /* COMM_HOST_H */
