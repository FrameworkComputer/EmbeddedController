/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_INCLUDE_EXTENSION_H
#define __EC_INCLUDE_EXTENSION_H

#include <stddef.h>
#include <stdint.h>

#include "common.h"

/*
 * Type of function handling extension commands.
 *
 * @param buffer        As input points to the input data to be processed, as
 *                      output stores data, processing result.
 * @param command_size  Number of bytes of input data
 * @param response_size On input - max size of the buffer, on output - actual
 *                      number of data returned by the handler.
 */
typedef void (*extension_handler)(void *buffer,
				 size_t command_size,
				 size_t *response_size);
/*
 * Find handler for an extension command.
 *
 * @param command_code Code associated with a extension command handler.
 * @param buffer       Data to be processd by the handler, the same space
 *                     is used for data returned by the handler.
 * @command_size       Size of the input data.
 * @param size On input - max size of the buffer, on output - actual number of
 *                     data returned by the handler. A single byte return
 *                     usually indicates an error and contains the error code.
 */
void extension_route_command(uint16_t command_code,
			    void *buffer,
			    size_t command_size,
			    size_t *size);

struct extension_command {
	uint16_t command_code;
	extension_handler handler;
} __packed;

/* Values for different extension subcommands. */
enum {
	EXTENSION_AES = 0,
	EXTENSION_HASH = 1,
	EXTENSION_RSA = 2,
	EXTENSION_EC = 3,
	EXTENSION_FW_UPGRADE = 4,
	EXTENSION_HKDF = 5,
	EXTENSION_ECIES = 6,
};


/* Error codes reported by extension commands. */
enum {
	/* EXTENSION_HASH error codes */
	/* Attempt to start a session on an active handle. */
	EXC_HASH_DUPLICATED_HANDLE = 1,
	EXC_HASH_TOO_MANY_HANDLES = 2,  /* No room to allocate a new context. */
	/* Continuation/finish on unknown context. */
	EXC_HASH_UNKNOWN_CONTEXT = 3
};

#define DECLARE_EXTENSION_COMMAND(code, handler) \
	const struct extension_command __keep __extension_cmd_##code \
	__attribute__((section(".rodata.extensioncmds")))	   \
	= {code, handler}

#endif  /* __EC_INCLUDE_EXTENSION_H */
