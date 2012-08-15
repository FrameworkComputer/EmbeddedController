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
	/*
	 * The driver that receives the command sets up the send_response()
	 * handler. Once the command is processed this handler is called to
	 * send the response back to the host.
	 */
	void (*send_response)(struct host_cmd_handler_args *args);
	uint8_t command;       /* Command (e.g., EC_CMD_FLASH_GET_INFO) */
	uint8_t version;       /* Version of command (0-31) */
	uint8_t params_size;   /* Size of input parameters in bytes */
	uint8_t i2c_old_response; /* (for I2C) send an old-style response */
	const void *params; /* Input parameters */
	/*
	 * Pointer to output response data buffer.  On input to the handler,
	 * points to a buffer of size response_max.  Command handler can change
	 * this to point to a different location instead of memcpy()'ing data
	 * into the provided buffer.
	 */
	void *response;
	/*
	 * Maximum size of response buffer provided to command handler.  If the
	 * handler changes response to point to its own larger buffer, it may
	 * return a response_size greater than response_max.
	 */
	uint8_t response_max;
	uint8_t response_size; /* Size of data pointed to by resp_ptr */

	/*
	 * This is the result returned by command and therefore the status to
	 * be reported from the command execution to the host. The driver
	 * should set this to EC_RES_SUCCESS on receipt of a valid command.
	 * It is then passed back to the driver via send_response() when
	 * command execution is complete. The driver may still override this
	 * when sending the response back to the host if it detects an error
	 * in the response or in its own operation.
	 */
	enum ec_status result;
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
 * Return the raw event state.
 */
uint32_t host_get_events(void);

/**
 * Send a response to the relevent driver for transmission
 *
 * Once command processing is complete, this is used to send a response
 * back to the host.
 *
 * @param args	Contains response to send
 */
void host_send_response(struct host_cmd_handler_args *args);

/**
 * Called by host interface module when a command is received.
 */
void host_command_received(struct host_cmd_handler_args *args);

/* Register a host command handler */
#define DECLARE_HOST_COMMAND(command, routine, version_mask)		\
	const struct host_command __host_cmd_##command			\
	__attribute__((section(".rodata.hcmds")))			\
	     = {command, routine, version_mask}

#endif  /* __CROS_EC_HOST_COMMAND_H */
