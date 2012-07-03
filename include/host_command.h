/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#ifndef __CROS_EC_HOST_COMMAND_H
#define __CROS_EC_HOST_COMMAND_H

#include "common.h"
#include "ec_commands.h"

/* Host command */
struct host_command {
	/* Command code. */
	int command;
	/* Handler for the command; data points to parameters/response.
	 * returns negative error code if case of failure (using EC_LPC_STATUS
	 * codes). sets <response_size> if it returns a payload to the host. */
	int (*handler)(uint8_t *data, int *response_size);
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
 * @param slot		is 0 for kernel-originated commands,
 *			1 for usermode-originated commands.
 * @param command	The command code
 * @param data		Buffer holding the command, and used for the
 * 			response payload.
 * @param response_size	Returns the size of the response
 * @return resulting status
 */
enum ec_status host_command_process(int slot, int command, uint8_t *data,
				    int *response_size);

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
 * Called by host interface module when a command is written to one of the
 * command slots (0=kernel, 1=user).
 */
void host_command_received(int slot, int command);

/* Send a successful result code along with response data to a host command.
 * <slot> is 0 for kernel-originated commands,
 *           1 for usermode-originated commands.
 * <result> is the result code for the command (EC_RES_...)
 * <data> is the buffer with the response payload.
 * <size> is the size of the response buffer. */
void host_send_response(int slot, enum ec_status result, const uint8_t *data,
			int size);

/* Return a pointer to the host command data buffer.  This buffer must
 * only be accessed between a notification to host_command_received()
 * and a subsequent call to lpc_SendHostResponse().  <slot> is 0 for
 * kernel-originated commands, 1 for usermode-originated commands. */
uint8_t *host_get_buffer(int slot);

/* Register a host command handler */
#define DECLARE_HOST_COMMAND(command, routine)				\
	const struct host_command __host_cmd_##command			\
	__attribute__((section(".rodata.hcmds")))			\
	     = {command, routine}

#endif  /* __CROS_EC_HOST_COMMAND_H */
