/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdio.h>
#include <sys/io.h>
#include <sys/param.h>
#include <unistd.h>

#include "comm-host.h"
#include "ec_commands.h"

#define INITIAL_UDELAY 5     /* 5 us */
#define MAXIMUM_UDELAY 10000 /* 10 ms */

int comm_init(void)
{
	int i;
	int byte = 0xff;

	/* Request I/O privilege */
	if (iopl(3) < 0) {
		perror("Error getting I/O privilege");
		return -3;
	}

	/*
	 * Test if the I/O port has been configured for Chromium EC LPC
	 * interface.  If all the bytes are 0xff, very likely that Chromium EC
	 * is not present.
	 *
	 * TODO: (crosbug.com/p/10963) Should only need to look at the command
	 * byte, since we don't support ACPI burst mode and thus bit 4 should
	 * be 0.
	 */
	byte &= inb(EC_LPC_ADDR_USER_CMD);
	byte &= inb(EC_LPC_ADDR_USER_DATA);
	for (i = 0; i < EC_PARAM_SIZE && byte == 0xff; ++i)
		byte &= inb(EC_LPC_ADDR_USER_PARAM + i);
	if (byte == 0xff) {
		fprintf(stderr, "Port 0x%x,0x%x,0x%x-0x%x are all 0xFF.\n",
			EC_LPC_ADDR_USER_CMD, EC_LPC_ADDR_USER_DATA,
			EC_LPC_ADDR_USER_PARAM,
			EC_LPC_ADDR_USER_PARAM + EC_PARAM_SIZE - 1);
		fprintf(stderr,
			"Very likely this board doesn't have a Chromium EC.\n");
		return -4;
	}

	return 0;
}

/*
 * Wait for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout.
 */
static int wait_for_ec(int status_addr, int timeout_usec)
{
	int i;
	int delay = INITIAL_UDELAY;

	for (i = 0; i < timeout_usec; i += delay) {
		/*
		 * Delay first, in case we just sent out a command but the EC
		 * hasn't raise the busy flag. However, I think this doesn't
		 * happen since the LPC commands are executed in order and the
		 * busy flag is set by hardware.
		 *
		 * TODO: move this delay after inb(status).
		 */
		usleep(MIN(delay, timeout_usec - i));

		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		/* Increase the delay interval after a few rapid checks */
		if (i > 20)
			delay = MIN(delay * 2, MAXIMUM_UDELAY);
	}
	return -1;  /* Timeout */
}

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
		return -EC_RES_ERROR;
	}

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC ready\n");
		return -EC_RES_ERROR;
	}

	/* Write data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)indata; i < insize; i++, d++)
		outb(*d, param_addr + i);

	outb(command, cmd_addr);

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC response\n");
		return -EC_RES_ERROR;
	}

	/* Check result */
	i = inb(data_addr);
	if (i) {
		fprintf(stderr, "EC returned error result code %d\n", i);
		return -i;
	}

	/* Read data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)outdata; i < outsize; i++, d++)
		*d = inb(param_addr + i);

	/*
	 * LPC protocol doesn't have a way to communicate the true output
	 * size, so assume we got everything we asked for.
	 */
	return outsize;
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
