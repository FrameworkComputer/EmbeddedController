/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_INCLUDE_EXTENSION_H
#define __EC_INCLUDE_EXTENSION_H

#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "tpm_vendor_cmds.h"

/* Flags for vendor or extension commands */
enum vendor_cmd_flags {
	/*
	 * Command is coming from the USB interface (either via the vendor
	 * command endpoint or the console).  If this flag is not present,
	 * the command is coming from the AP.
	 */
	VENDOR_CMD_FROM_USB = (1 << 0),
};

/*
 * Type of function handling extension commands.
 *
 * @param buffer        As input points to the input data to be processed, as
 *                      output stores data, processing result.
 * @param command_size  Number of bytes of input data
 * @param response_size On input - max size of the buffer, on output - actual
 *                      number of data returned by the handler.
 */
typedef enum vendor_cmd_rc (*extension_handler)(enum vendor_cmd_cc code,
						void *buffer,
						size_t command_size,
						size_t *response_size);

/**
 * Find handler for an extension command.
 *
 * Use the interface specific function call in order to check the policies for
 * handling the commands on that interface.
 *
 * @param command_code Code associated with a extension command handler.
 * @param buffer       Data to be processd by the handler, the same space
 *                     is used for data returned by the handler.
 * @param in_size      Size of the input data.
 * @param out_size     On input: max size of the buffer.  On output: actual
 *                     number of bytes returned by the handler; a single byte
 *                     usually indicates an error and contains the error code.
 * @param flags        Zero or more flags from vendor_cmd_flags.
 */
uint32_t extension_route_command(uint16_t command_code,
				 void *buffer,
				 size_t in_size,
				 size_t *out_size,
				 uint32_t flags);

/* Pointer table */
struct extension_command {
	uint16_t command_code;
	extension_handler handler;
} __packed;

#define DECLARE_EXTENSION_COMMAND(code, func)				\
	static enum vendor_cmd_rc func##_wrap(enum vendor_cmd_cc code,	\
				       void *cmd_body,			\
				       size_t cmd_size,			\
				       size_t *response_size) {		\
		func(cmd_body, cmd_size, response_size);		\
		return 0;						\
	}								\
	const struct extension_command __keep __extension_cmd_##code	\
	__attribute__((section(".rodata.extensioncmds")))		\
		= {.command_code = code, .handler = func##_wrap }

#define DECLARE_VENDOR_COMMAND(code, func)				\
	const struct extension_command __keep __vendor_cmd_##code	\
	__attribute__((section(".rodata.extensioncmds")))		\
		= {.command_code = code, .handler = func}

#endif  /* __EC_INCLUDE_EXTENSION_H */
