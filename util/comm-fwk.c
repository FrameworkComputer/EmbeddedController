/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* The I/O asm funcs exist only on x86. */
#if defined(__i386__) || defined(__x86_64__)

#include <stdint.h>
#include <stdio.h>
#include <sys/io.h>
#include <sys/param.h>
#include <unistd.h>
#include <string.h>

#include "comm-host.h"
#include "lock/gec_lock.h"

#define INITIAL_UDELAY 5 /* 5 us */
#define MAXIMUM_UDELAY 10000 /* 10 ms */

typedef enum _ec_transaction_direction { EC_TX_WRITE, EC_TX_READ } ec_transaction_direction;

// As defined in MEC172x section 16.8.3
// https://ww1.microchip.com/downloads/en/DeviceDoc/MEC172x-Data-Sheet-DS00003583C.pdf
#define FW_EC_BYTE_ACCESS               0x00
#define FW_EC_LONG_ACCESS_AUTOINCREMENT 0x03

#define FW_EC_EC_ADDRESS_REGISTER0      0x0802
#define FW_EC_EC_ADDRESS_REGISTER1      0x0803
#define FW_EC_EC_DATA_REGISTER0         0x0804
#define FW_EC_EC_DATA_REGISTER1         0x0805
#define FW_EC_EC_DATA_REGISTER2         0x0806
#define FW_EC_EC_DATA_REGISTER3         0x0807

static int ec_transact(ec_transaction_direction direction, uint16_t address,
		       char *data, uint16_t size)
{
	int pos = 0;
	uint16_t temp[2];
	if (address % 4 > 0) {
		outw((address & 0xFFFC) | FW_EC_BYTE_ACCESS, FW_EC_EC_ADDRESS_REGISTER0);
		/* Unaligned start address */
		for (int i = address % 4; i < 4; ++i) {
			char *storage = &data[pos++];
			if (direction == EC_TX_WRITE)
				outb(*storage, FW_EC_EC_DATA_REGISTER0 + i);
			else if (direction == EC_TX_READ)
				*storage = inb(FW_EC_EC_DATA_REGISTER0 + i);
		}
		address = (address + 4) & 0xFFFC; // Up to next multiple of 4
	}

	if (size - pos >= 4) {
		outw((address & 0xFFFC) | FW_EC_LONG_ACCESS_AUTOINCREMENT, FW_EC_EC_ADDRESS_REGISTER0);
		// Chunk writing for anything large, 4 bytes at a time
		// Writing to 804, 806 automatically increments dest address
		while (size - pos >= 4) {
			if (direction == EC_TX_WRITE) {
				memcpy(temp, &data[pos], sizeof(temp));
				outw(temp[0], FW_EC_EC_DATA_REGISTER0);
				outw(temp[1], FW_EC_EC_DATA_REGISTER2);
			} else if (direction == EC_TX_READ) {
				temp[0] = inw(FW_EC_EC_DATA_REGISTER0);
				temp[1] = inw(FW_EC_EC_DATA_REGISTER2);
				memcpy(&data[pos], temp, sizeof(temp));
			}

			pos += 4;
			address += 4;
		}
	}

	if (size - pos > 0) {
		// Unaligned remaining data - R/W it by byte
		outw((address & 0xFFFC) | FW_EC_BYTE_ACCESS, FW_EC_EC_ADDRESS_REGISTER0);
		for (int i = 0; i < (size - pos); ++i) {
			char *storage = &data[pos + i];
			if (direction == EC_TX_WRITE)
				outb(*storage, FW_EC_EC_DATA_REGISTER0 + i);
			else if (direction == EC_TX_READ)
				*storage = inb(FW_EC_EC_DATA_REGISTER0 + i);
		}
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
		 * hasn't raised the busy flag.  However, I think this doesn't
		 * happen since the LPC commands are executed in order and the
		 * busy flag is set by hardware.  Minor issue in any case,
		 * since the initial delay is very short.
		 */
		usleep(MIN(delay, timeout_usec - i));

		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		/* Increase the delay interval after a few rapid checks */
		if (i > 20)
			delay = MIN(delay * 2, MAXIMUM_UDELAY);
	}
	return -1; /* Timeout */
}

static uint8_t ec_checksum_buffer(char *data, size_t size)
{
	uint8_t sum = 0;
	for (int i = 0; i < size; ++i) {
		sum += data[i];
	}
	return sum;
};

static int ec_command_lpc_3(int command, int version, const void *outdata,
			    int outsize, void *indata, int insize)
{
	uint8_t csum = 0;
	int i;

	union {
		struct ec_host_request rq;
		char data[EC_LPC_HOST_PACKET_SIZE];
	} u;

	union {
		struct ec_host_response rs;
		char data[EC_LPC_HOST_PACKET_SIZE];
	} r;

	/* Fail if output size is too big */
	if (outsize + sizeof(u.rq) > EC_LPC_HOST_PACKET_SIZE)
		return -EC_RES_REQUEST_TRUNCATED;

	/* Fill in request packet */
	/* TODO(crosbug.com/p/23825): This should be common to all protocols */
	u.rq.struct_version = EC_HOST_REQUEST_VERSION;
	u.rq.checksum = 0;
	u.rq.command = command;
	u.rq.command_version = version;
	u.rq.reserved = 0;
	u.rq.data_len = outsize;

	memcpy(&u.data[sizeof(u.rq)], outdata, outsize);
	csum = ec_checksum_buffer(u.data, outsize + sizeof(u.rq));
	u.rq.checksum = (uint8_t)(-csum);

	if (wait_for_ec(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC response\n");
		return -EC_RES_ERROR;
	}

	ec_transact(EC_TX_WRITE, 0, u.data, outsize + sizeof(u.rq));

	/* Start the command */
	outb(EC_COMMAND_PROTOCOL_3, EC_LPC_ADDR_HOST_CMD);

	if (wait_for_ec(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC response\n");
		return -EC_RES_ERROR;
	}

	/* Check result */
	i = inb(EC_LPC_ADDR_HOST_DATA);
	if (i) {
		fprintf(stderr, "EC returned error result code %d\n", i);
		return -EECRESULT - i;
	}

	csum = 0;
	ec_transact(EC_TX_READ, 0, r.data, sizeof(r.rs));

	if (r.rs.struct_version != EC_HOST_RESPONSE_VERSION) {
		fprintf(stderr, "EC response version mismatch\n");
		return -EC_RES_INVALID_RESPONSE;
	}

	if (r.rs.reserved) {
		fprintf(stderr, "EC response reserved != 0\n");
		return -EC_RES_INVALID_RESPONSE;
	}

	if (r.rs.data_len > insize) {
		fprintf(stderr, "EC returned too much data\n");
		return -EC_RES_RESPONSE_TOO_BIG;
	}

	if (r.rs.data_len > 0) {
		ec_transact(EC_TX_READ, 8, r.data + sizeof(r.rs), r.rs.data_len);
		if (ec_checksum_buffer(r.data, sizeof(r.rs) + r.rs.data_len)) {
			fprintf(stderr, "EC response has invalid checksum\n");
			return -EC_RES_INVALID_CHECKSUM;
		}

		memcpy(indata, r.data + sizeof(r.rs), r.rs.data_len);
	}
	return r.rs.data_len;
}

static int ec_readmem_fwk(int offset, int bytes, void *dest)
{
	ec_transact(EC_TX_READ, (EC_LPC_ADDR_MEMMAP - EC_HOST_CMD_REGION0) | (offset & 0x7FFF), dest, bytes);
	return bytes;
}

int comm_init_fwk(void)
{
	/* Request I/O privilege */
	if (iopl(3) < 0) {
		perror("Error getting I/O privilege");
		release_gec_lock();
		return -3;
	}

	ec_command_proto = ec_command_lpc_3;

	ec_max_outsize =
		EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_request);
	ec_max_insize =
		EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_response);

	ec_readmem = ec_readmem_fwk;
	return 0;
}

#endif
