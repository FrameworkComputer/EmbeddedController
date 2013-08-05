/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * For hysterical raisins, there are several mechanisms for communicating with
 * the EC. This abstracts them.
 */

#ifndef COMM_HOST_H
#define COMM_HOST_H

#include "common.h"
#include "ec_commands.h"

/* ec_command return value for non-success result from EC */
#define EECRESULT 1000

/* Maximum output and input sizes for EC command, in bytes */
extern int ec_max_outsize, ec_max_insize;

/*
 * Maximum-size output and input buffers, for use by callers.  This saves each
 * caller needing to allocate/free its own buffers.
 */
extern void *ec_outbuf;
extern void *ec_inbuf;

/**
 * Perform initializations needed for subsequent requests
 *
 * returns 0 in case of success or error code. */
int comm_init(void);

/**
 * Send a command to the EC.  Returns the length of output data returned (0 if
 * none), or negative on error.
 */
extern int (*ec_command)(int command, int version,
			 const void *outdata, int outsize, /* to the EC */
			 void *indata, int insize);	   /* from the EC */

/**
 * Return the content of the EC information area mapped as "memory".
 * The offsets are defined by the EC_MEMMAP_ constants. Returns the number
 * of bytes read, or negative on error. Specifying bytes=0 will read a
 * string (always including the trailing '\0').
 */
extern int (*ec_readmem)(int offset, int bytes, void *dest);

#endif /* COMM_HOST_H */
