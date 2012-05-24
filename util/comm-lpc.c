/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdio.h>
#include <sys/io.h>
#include <unistd.h>

#include "comm-host.h"
#include "ec_commands.h"


int comm_init(void)
{
	/* Request I/O privilege */
	if (iopl(3) < 0) {
		perror("Error getting I/O privilege");
		return -3;
	}
	return 0;
}


/* Waits for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout. */
static int wait_for_ec(int status_addr, int timeout_usec)
{
	int i;
	for (i = 0; i < timeout_usec; i += 10) {
		usleep(10);  /* Delay first, in case we just sent a command */
		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;
	}
	return -1;  /* Timeout */
}

/* Sends a command to the EC.  Returns the command status code, or
 * -1 if other error. */
int ec_command(int command, const void *indata, int insize,
	       void *outdata, int outsize) {
	uint8_t *d;
	int i;

	/* TODO: add command line option to use kernel command/param window */
	int cmd_addr = EC_LPC_ADDR_USER_CMD;
	int data_addr = EC_LPC_ADDR_USER_DATA;
	int param_addr = EC_LPC_ADDR_USER_PARAM;

	if (insize > EC_PARAM_SIZE || outsize > EC_PARAM_SIZE) {
		fprintf(stderr, "Data size too big\n");
		return -1;
	}

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC ready\n");
		return -1;
	}

	/* Write data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)indata; i < insize; i++, d++)
		outb(*d, param_addr + i);

	outb(command, cmd_addr);

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC response\n");
		return -1;
	}

	/* Check result */
	i = inb(data_addr);
	if (i) {
		fprintf(stderr, "EC returned error result code %d\n", i);
		return i;
	}

	/* Read data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)outdata; i < outsize; i++, d++)
		*d = inb(param_addr + i);

	return 0;
}


uint8_t read_mapped_mem8(uint8_t offset)
{
	return inb(EC_LPC_ADDR_MEMMAP + offset);
}


uint16_t read_mapped_mem16(uint8_t offset)
{
	return inw(EC_LPC_ADDR_MEMMAP + offset);
}


uint32_t read_mapped_mem32(uint8_t offset)
{
	return inl(EC_LPC_ADDR_MEMMAP + offset);
}


int read_mapped_string(uint8_t offset, char *buf)
{
	int c;

	for (c = 0; c < EC_MEMMAP_TEXT_MAX; c++) {
		buf[c] = inb(EC_LPC_ADDR_MEMMAP + offset + c);
		if (buf[c] == 0)
			return c;
	}

	buf[EC_MEMMAP_TEXT_MAX-1] = 0;
	return EC_MEMMAP_TEXT_MAX;
}
