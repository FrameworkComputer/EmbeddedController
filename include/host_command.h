/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#ifndef __CROS_EC_HOST_COMMAND_H
#define __CROS_EC_HOST_COMMAND_H

#include "common.h"
#include "ec_commands.h"

/* Args for host command handler */
struct host_cmd_handler_args {
	uint8_t command;       /* Command (e.g., EC_CMD_FLASH_GET_INFO) */
	uint8_t version;       /* Version of command (0-31) */
	const uint8_t *params; /* Input parameters */
	uint8_t params_size;   /* Size of input parameters in bytes */
	/*
	 * Pointer to output response data buffer.  On input to the handler,
	 * points to a buffer of size response_max.  Command handler can change
	 * this to point to a different location instead of memcpy()'ing data
	 * into the provided buffer.
	 */
	uint8_t *response;
	/*
	 * Maximum size of response buffer provided to command handler.  If the
	 * handler changes response to point to its own larger buffer, it may
	 * return a response_size greater than response_max.
	 */
	uint8_t response_max;
	uint8_t response_size; /* Size of data pointed to by resp_ptr */
};

/* Host command */
struct host_command {
	/* Command code */
	int command;
	/*
	 * Handler for the command.  Args points to context for handler.
	 * Returns result status (EC_RES_*).
	 */
	int (*handler)(struct host_cmd_handler_args *args);
	/* Mask of supported versions */
	int version_mask;
};

/**
 * Return a pointer to the memory-mapped buffer.
 *
 * This buffer is EC_MEMMAP_SIZE bytes long, is writable at any time, and the
 * host can read it at any time.
 *
 * @param offset        Offset within the range to return
 * @return pointer to the buffer at that offset
 */
uint8_t *host_get_memmap(int offset);

/**
 * Process a host command and return its response
 *
 * @param args	        Command handler args
 * @return resulting status
 */
enum ec_status host_command_process(struct host_cmd_handler_args *args);

/**
 * Set one or more host event bits.
 *
 * @param mask          Event bits to set (use EC_HOST_EVENT_MASK()).
 */
void host_set_events(uint32_t mask);

/**
 * Set a single host event.
 *
 * @param event         Event to set (EC_HOST_EVENT_*).
 */
static inline void host_set_single_event(int event)
{
	host_set_events(EC_HOST_EVENT_MASK(event));
}

/**
 * Clear one or more host event bits.
 *
 * @param mask          Event bits to clear (use EC_HOST_EVENT_MASK()).
 *                      Write 1 to a bit to clear it.
 */
void host_clear_events(uint32_t mask);

/**
 * Return the raw SCI/SMI event state.
 */
uint32_t host_get_events(void);

/**
 * Called by host interface module when a command is received.
 */
void host_command_received(struct host_cmd_handler_args *args);

/**
 * Send a successful result code along with response data to a host command.
 *
 * @param result        Result code for the command (EC_RES_...)
 * @param data          Buffer with the response payload.
 * @param size          Size of the response buffer.
 */
void host_send_response(enum ec_status result, const uint8_t *data, int size);

/* Register a host command handler */
#define DECLARE_HOST_COMMAND(command, routine, version_mask)		\
	const struct host_command __host_cmd_##command			\
	__attribute__((section(".rodata.hcmds")))			\
	     = {command, routine, version_mask}

#endif  /* __CROS_EC_HOST_COMMAND_H */
