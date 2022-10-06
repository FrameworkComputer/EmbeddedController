/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * For hysterical raisins, there are several mechanisms for communicating with
 * the EC. This abstracts them.
 */

#ifndef __UTIL_COMM_HOST_H
#define __UTIL_COMM_HOST_H

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
	COMM_DEV = BIT(0),
	COMM_LPC = BIT(1),
	COMM_I2C = BIT(2),
	COMM_SERVO = BIT(3),
	COMM_USB = BIT(4),
	COMM_ALL = -1
};

/**
 * Initialize alternative interfaces
 *
 * @param interfaces	Interfaces to try; use COMM_ALL to try all of them.
 * @param device_name For DEV option, the device file to use.
 * @param i2c_bus For I2C option, the bus number to use (or -1 to autodetect).
 * @return 0 in case of success, or error code.
 */
int comm_init_alt(int interfaces, const char *device_name, int i2c_bus);

/**
 * Initialize dev interface
 *
 * @return 0 in case of success, or error code.
 */
int comm_init_dev(const char *device_name);

/**
 * Initialize input & output buffers
 *
 * @return 0 in case of success, or error code.
 */
int comm_init_buffer(void);

/**
 * Send a command to the EC.  Returns the length of output data returned (0 if
 * none), or negative on error.
 */
int ec_command(int command, int version, const void *outdata,
	       int outsize, /* to
			       the
			       EC
			     */
	       void *indata, int insize); /* from the EC */

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
extern int (*ec_command_proto)(int command, int version, const void *outdata,
			       int outsize, /* to EC */
			       void *indata, int insize); /* from EC */

/**
 * Return the content of the EC information area mapped as "memory".
 * The offsets are defined by the EC_MEMMAP_ constants. Returns the number
 * of bytes read, or negative on error. Specifying bytes=0 will read a
 * string (always including the trailing '\0').
 */
extern int (*ec_readmem)(int offset, int bytes, void *dest);

/**
 * Wait for a MKBP event matching 'mask' for at most 'timeout' milliseconds.
 * Then read the incoming event content in 'buffer' (or at most
 * 'buf_size' bytes of it).
 * Return the size of the event read on success, 0 in case of timeout,
 * or a negative value in case of error.
 */
extern int (*ec_pollevent)(unsigned long mask, void *buffer, size_t buf_size,
			   int timeout);

#endif /* __UTIL_COMM_HOST_H */
