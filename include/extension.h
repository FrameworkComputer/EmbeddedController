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

/*
 * Find handler for an extension command.
 * @param command_code Code associated with a extension command handler.
 * @param buffer       Data to be processd by the handler, the same space
 *                     is used for data returned by the handler.
 * @command_size       Size of the input data.
 * @param size On input - max size of the buffer, on output - actual number of
 *                     data returned by the handler. A single byte return
 *                     usually indicates an error and contains the error code.
 */
uint32_t extension_route_command(uint16_t command_code,
				 void *buffer,
				 size_t command_size,
				 size_t *size);

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
