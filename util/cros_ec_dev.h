/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __UTIL_CROS_EC_DEV_H
#define __UTIL_CROS_EC_DEV_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CROS_EC_DEV_NAME "cros_ec"
#define CROS_EC_DEV_VERSION "1.0.0"

/*
 * @version: Command version number (often 0)
 * @command: Command to send (EC_CMD_...)
 * @outdata: Outgoing data to EC
 * @outsize: Outgoing length in bytes
 * @indata: Where to put the incoming data from EC
 * @insize: On call, how much we can accept. On return, how much we got.
 * @result: EC's response to the command (separate from communication failure)
 * ioctl returns zero on success, negative on error
 */
struct cros_ec_command {
	uint32_t version;
	uint32_t command;
	uint8_t *outdata;
	uint32_t outsize;
	uint8_t *indata;
	uint32_t insize;
	uint32_t result;
};

/*
 * @offset: within EC_LPC_ADDR_MEMMAP region
 * @bytes: number of bytes to read. zero means "read a string" (including '\0')
 *         (at most only EC_MEMMAP_SIZE bytes can be read)
 * @buffer: where to store the result
 * ioctl returns the number of bytes read, negative on error
 */
struct cros_ec_readmem {
	uint32_t offset;
	uint32_t bytes;
	char *buffer;
};

#define CROS_EC_DEV_IOC ':'
#define CROS_EC_DEV_IOCXCMD _IOWR(':', 0, struct cros_ec_command)
#define CROS_EC_DEV_IOCRDMEM _IOWR(':', 1, struct cros_ec_readmem)

/*
 * @version: Command version number (often 0)
 * @command: Command to send (EC_CMD_...)
 * @outsize: Outgoing length in bytes
 * @insize: Max number of bytes to accept from EC
 * @result: EC's response to the command (separate from communication failure)
 * @data: Where to put the incoming data from EC and outgoing data to EC
 */
struct cros_ec_command_v2 {
	uint32_t version;
	uint32_t command;
	uint32_t outsize;
	uint32_t insize;
	uint32_t result;
	uint8_t data[0];
};

/*
 * @offset: within EC_LPC_ADDR_MEMMAP region
 * @bytes: number of bytes to read. zero means "read a string" (including '\0')
 *         (at most only EC_MEMMAP_SIZE bytes can be read)
 * @buffer: where to store the result
 * ioctl returns the number of bytes read, negative on error
 */
struct cros_ec_readmem_v2 {
	uint32_t offset;
	uint32_t bytes;
	uint8_t buffer[EC_MEMMAP_SIZE];
};

#define CROS_EC_DEV_IOC_V2 0xEC
#define CROS_EC_DEV_IOCXCMD_V2 \
	_IOWR(CROS_EC_DEV_IOC_V2, 0, struct cros_ec_command_v2)
#define CROS_EC_DEV_IOCRDMEM_V2 \
	_IOWR(CROS_EC_DEV_IOC_V2, 1, struct cros_ec_readmem_v2)
#define CROS_EC_DEV_IOCEVENTMASK_V2 _IO(CROS_EC_DEV_IOC_V2, 2)

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_CROS_EC_DEV_H */
