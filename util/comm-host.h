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

/* Interfaces to allow for comm_init() */
enum comm_interface {
	COMM_DEV = (1 << 0),
	COMM_LPC = (1 << 1),
	COMM_I2C = (1 << 2),
	COMM_ALL = -1
};

/**
 * Perform initializations needed for subsequent requests
 *
 * @param interfaces	Interfaces to try; use COMM_ALL to try all of them.
 * @param device_name For DEV option, the device file to use.
 * @return 0 in case of success, or error code.
 */
int comm_init(int interfaces, const char *device_name);

/**
 * Send a command to the EC.  Returns the length of output data returned (0 if
 * none), or negative on error.
 */
int ec_command(int command, int version,
	       const void *outdata, int outsize,   /* to the EC */
	       void *indata, int insize);	   /* from the EC */

/**
 * Set the offset to be applied to the command number when ec_command() calls
 * ec_command_proto().
 */
void set_command_offset(int offset);

/**
 * Send a command to the EC.  Returns the length of output data returned (0 if
 * none), or negative on error.  This is the low-level interface implemented
 * by the protocol-specific driver.  DO NOT call this version directly from
 * anywhere but ec_command(), or the --device option will not work.
 */
extern int (*ec_command_proto)(int command, int version,
			       const void *outdata, int outsize, /* to EC */
			       void *indata, int insize);        /* from EC */

/**
 * Return the content of the EC information area mapped as "memory".
 * The offsets are defined by the EC_MEMMAP_ constants. Returns the number
 * of bytes read, or negative on error. Specifying bytes=0 will read a
 * string (always including the trailing '\0').
 */
extern int (*ec_readmem)(int offset, int bytes, void *dest);

#endif /* COMM_HOST_H */
