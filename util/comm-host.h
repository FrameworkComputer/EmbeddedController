/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef COMM_HOST_H
#define COMM_HOST_H

#include <stdint.h>

/* Perform initializations needed for subsequent requests
 *
 * returns 0 in case of success or error code. */
int comm_init(void);

/* Sends a command to the EC.  Returns the command status code, or
 * -1 if other error. */
int ec_command(int command, const void *indata, int insize,
	       void *outdata, int outsize);

/* Returns the content of the EC information area mapped as "memory".
 *
 * the offsets are defined by the EC_MEMMAP_ constants. */
uint8_t read_mapped_mem8(uint8_t offset);
uint16_t read_mapped_mem16(uint8_t offset);
uint32_t read_mapped_mem32(uint8_t offset);
int read_mapped_string(uint8_t offset, char *buf);

#endif /* COMM_HOST_H */
