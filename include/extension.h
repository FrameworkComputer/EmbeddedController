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
	VENDOR_CMD_FROM_USB = BIT(0),
};

/* Parameters for vendor commands */
struct vendor_cmd_params {
	/* Command code */
	enum vendor_cmd_cc code;

	/* On input, data to be processed.  On output, response data. */
	void *buffer;

	/* Number of bytes of input data */
	size_t in_size;

	/*
	 * On input, size of output buffer.  On output, actual response size.
	 * Both in bytes.  A single response byte usually indicates an error
	 * and contains the error code.
	 */
	size_t out_size;

	/* Flags; zero or more of enum vendor_cmd_flags */
	uint32_t flags;
};

/* Type of function handling extension commands. */
typedef enum vendor_cmd_rc
		(*extension_handler)(struct vendor_cmd_params *params);

/**
 * Find handler for an extension command.
 *
 * Use the interface specific function call in order to check the policies for
 * handling the commands on that interface.
 *
 * @param p		Parameters for the command
 * @return The return code from processing the command.
 */
uint32_t extension_route_command(struct vendor_cmd_params *p);


/* Pointer table */
struct extension_command {
	uint16_t command_code;
	extension_handler handler;
} __packed;

#define DECLARE_EXTENSION_COMMAND(code, func)				\
	static enum vendor_cmd_rc					\
	func##_wrap(struct vendor_cmd_params *params)			\
	{								\
		func(params->buffer, params->in_size,			\
		     &params->out_size);				\
		return VENDOR_RC_SUCCESS;				\
	}								\
	const struct extension_command __keep  __no_sanitize_address	\
	__extension_cmd_##code						\
	__attribute__((section(".rodata.extensioncmds")))		\
		= {.command_code = code, .handler = func##_wrap }

/* Vendor command which takes params directly */
#define DECLARE_VENDOR_COMMAND(cmd_code, func)				\
	static enum vendor_cmd_rc					\
	func##_wrap(struct vendor_cmd_params *params)			\
	{								\
		return func(params->code, params->buffer,		\
		      params->in_size, &params->out_size);		\
	}								\
	const struct extension_command __keep  __no_sanitize_address	\
	__vendor_cmd_##cmd_code						\
	__attribute__((section(".rodata.extensioncmds")))		\
		= {.command_code = cmd_code, .handler = func##_wrap}

/* Vendor command which takes params as struct */
#define DECLARE_VENDOR_COMMAND_P(cmd_code, func)			\
	const struct extension_command __keep  __no_sanitize_address	\
	__vendor_cmd_##cmd_code						\
	__attribute__((section(".rodata.extensioncmds")))		\
		= {.command_code = cmd_code, .handler = func}

#endif  /* __EC_INCLUDE_EXTENSION_H */
