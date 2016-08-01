/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <endian.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ftdi_spi_tpm.h"

static struct mpsse_context *mpsse_;
static unsigned locality_;	/* Set at initialization. */
static int ftdi_trace_enabled;

/* Assorted TPM2 registers for interface type FIFO. */
#define TPM_LOCALITY_0_SPI_BASE 0x00d40000

#define TPM_ACCESS_REG    (TPM_LOCALITY_0_SPI_BASE + 0)
#define TPM_STS_REG       (TPM_LOCALITY_0_SPI_BASE + 0x18)
#define TPM_DATA_FIFO_REG (TPM_LOCALITY_0_SPI_BASE + 0x24)
#define TPM_DID_VID_REG   (TPM_LOCALITY_0_SPI_BASE + 0xf00)
#define TPM_RID_REG       (TPM_LOCALITY_0_SPI_BASE + 0xf04)
#define TPM_FW_VER        (TPM_LOCALITY_0_SPI_BASE + 0xf90)

static struct swig_string_data empty_string_data = (struct swig_string_data){
	.size = 0, .data = NULL
};

/* Locality management bits (in TPM_ACCESS_REG). */
enum TpmAccessBits {
	tpmRegValidSts = (1 << 7),
	activeLocality = (1 << 5),
	requestUse = (1 << 1),
	tpmEstablishment = (1 << 0),
};

enum TpmStsBits {
	tpmFamilyShift = 26,
	tpmFamilyMask = ((1 << 2) - 1),	/* 2 bits wide. */
	tpmFamilyTPM2 = 1,
	resetEstablishmentBit = (1 << 25),
	commandCancel = (1 << 24),
	burstCountShift = 8,
	burstCountMask = ((1 << 16) - 1),	/* 16 bits wide. */
	stsValid = (1 << 7),
	commandReady = (1 << 6),
	tpmGo = (1 << 5),
	dataAvail = (1 << 4),
	Expect = (1 << 3),
	selfTestDone = (1 << 2),
	responseRetry = (1 << 1),
};

enum {
	false = 0,
	true = 1
};

/*
 * SPI frame header for TPM transactions is 4 bytes in size, it is described
 * in section "6.4.6 Spi Bit Protocol" of the TCG issued "TPM Profile (PTP)
 * Specification Revision 00.43.
 */
struct SpiFrameHeader {
	unsigned char body[4];
};

void FtdiStop(void)
{
	if (mpsse_)
		Close(mpsse_);

	mpsse_ = NULL;
}

/*
 * If the TPM is asleep we may need to poke it once to wake it up. Just assert
 * the CS briefly without sending any data, then wait a bit to be sure it's
 * awake.
 */
static void FtdiSpiPoke(void)
{
	Start(mpsse_);
	usleep(1000);
	Stop(mpsse_);
	usleep(60000);
}

static void StartTransaction(int read_write, size_t bytes, unsigned addr)
{
	struct SpiFrameHeader header;
	int i;
	uint8_t flow_c;
	char *transfer_data;

	/*
	 * give it 10 ms. TODO(vbendeb): remove this once cr50 SPS TPM driver
	 * performance is fixed.
	 */
	usleep(10000);

	/*
	 * The first byte of the frame header encodes the transaction type
	 * (read or write) and size (set to length - 1).
	 */
	header.body[0] = (read_write ? 0x80 : 0) | 0x40 | (bytes - 1);

	/* The rest of the frame header is the internal address in the TPM. */
	for (i = 0; i < 3; i++)
		header.body[i + 1] = (addr >> (8 * (2 - i))) & 0xff;

	Start(mpsse_);

	transfer_data =
	    Transfer(mpsse_, (char *)header.body, sizeof(header.body));

	/*
	 * The TCG TPM over SPI specification itroduces the notion of SPI flow
	 * control (Section "6.4.5 Flow Control" of the TCG issued "TPM
	 * Profile (PTP) Specification Revision 00.43).
	 *
	 * The slave (TPM device) expects each transaction to start with a 4
	 * byte header trasmitted by master. If the slave needs to stall the
	 * transaction, it sets the MOSI bit to 0 during the last clock of the
	 * 4 byte header. In this case the master is supposed to start polling
	 * the line, byte at time, until the last bit in the received byte
	 * (transferred during the last clock of the byte) is set to 1.
	 */
	flow_c = transfer_data[3];
	free(transfer_data);
	while (!(flow_c & 1)) {
		transfer_data = Read(mpsse_, 1);
		flow_c = transfer_data[0];
		free(transfer_data);
	}
}

static void trace_dump(const char *prefix, unsigned reg, size_t bytes,
		       const uint8_t *buffer)
{
	if (!ftdi_trace_enabled)
		return;
	printf("%s %2.2x:", prefix, reg);
	if (bytes == 4) {
		printf(" %8.8x\n", *(const uint32_t *)buffer);
	} else {
		int i;

		for (i = 0; i < bytes; i++)
			printf(" %2.2x", buffer[i]);
		printf("\n");
	}
}

static int FtdiWriteReg(unsigned reg_number, size_t bytes, void *buffer)
{
	if (!mpsse_)
		return false;

	trace_dump("W", reg_number, bytes, buffer);
	StartTransaction(false, bytes, reg_number + locality_ * 0x10000);
	Write(mpsse_, buffer, bytes);
	Stop(mpsse_);
	return true;
}

static int FtdiReadReg(unsigned reg_number, size_t bytes, void *buffer)
{
	void *data;

	if (!mpsse_)
		return false;

	StartTransaction(true, bytes, reg_number + locality_ * 0x10000);
	data = Read(mpsse_, bytes);
	if (data)
		memcpy(buffer, data, bytes);
	free(data);
	Stop(mpsse_);
	trace_dump("R", reg_number, bytes, buffer);
	return true;
}

static int ReadTpmSts(uint32_t *status)
{
	return FtdiReadReg(TPM_STS_REG, sizeof(*status), status);
}

static int WriteTpmSts(uint32_t status)
{
	return FtdiWriteReg(TPM_STS_REG, sizeof(status), &status);
}

static uint32_t GetBurstCount(void)
{
	uint32_t status;

	ReadTpmSts(&status);
	return (status >> burstCountShift) & burstCountMask;
}

static void GetVersion(void)
{
	int chunk_count = 0;
	uint32_t chunk = 0;
	char vstr[sizeof(chunk) + 1];           /* room for 4 chars + zero */

	/*
	 * Does not really matter what's written, this just makes sure
	 * the version is reported from the beginning.
	 */
	FtdiWriteReg(TPM_FW_VER, sizeof(chunk), &chunk);

	/* Print it out in 4 byte chunks. */
	vstr[sizeof(vstr) - 1] = 0;
	do {
		FtdiReadReg(TPM_FW_VER, sizeof(chunk), vstr);
		printf("%s", vstr);

		/*
		 * While string is not over, and no more than 200
		 * characters.
		 * This is likely result in one extra printk()
		 * invocation with an empty string, not a big deal.
		 */
	} while (vstr[0] && (chunk_count++ < (400 / sizeof(chunk))));

	printf("\n");
}

int FtdiSpiInit(uint32_t freq, int enable_debug)
{
	uint32_t did_vid, status;
	uint8_t cmd;
	uint16_t vid;

	if (mpsse_)
		return true;

	ftdi_trace_enabled = enable_debug;

	/* round frequency down to the closest 100KHz */
	freq = (freq / (100 * 1000)) * 100 * 1000;

	printf("Starting MPSSE at %d kHz\n", freq / 1000);
	mpsse_ = MPSSE(freq, MSB, NULL);
	if (!mpsse_)
		return false;

	/* Just in case, make sure bootsrap is not triggered. */
	PinLow(mpsse_, GPIOL0);

	FtdiSpiPoke();

	FtdiReadReg(TPM_DID_VID_REG, sizeof(did_vid), &did_vid);

	vid = did_vid & 0xffff;
	if ((vid != 0x15d1) && (vid != 0x1ae0)) {
		fprintf(stderr, "unknown did_vid: %#x\n", did_vid);
		return false;
	}

	/* Try claiming locality zero. */
	FtdiReadReg(TPM_ACCESS_REG, sizeof(cmd), &cmd);
	if ((cmd & (activeLocality & tpmRegValidSts)) ==
	    (activeLocality & tpmRegValidSts)) {
		/*
		 * Locality active - maybe reset line is not connected?
		 * Release the locality and try again
		 */
		cmd = activeLocality;
		FtdiWriteReg(TPM_ACCESS_REG, sizeof(cmd), &cmd);
		FtdiReadReg(TPM_ACCESS_REG, sizeof(cmd), &cmd);
	}

	/* tpmEstablishment can be either set or not. */
	if ((cmd & ~tpmEstablishment) != tpmRegValidSts) {
		fprintf(stderr, "invalid reset status: %#x\n", cmd);
		return false;
	}
	cmd = requestUse;
	FtdiWriteReg(TPM_ACCESS_REG, sizeof(cmd), &cmd);
	FtdiReadReg(TPM_ACCESS_REG, sizeof(cmd), &cmd);
	if ((cmd & ~tpmEstablishment) != (tpmRegValidSts | activeLocality)) {
		fprintf(stderr, "failed to claim locality, status: %#x\n", cmd);
		return false;
	}

	ReadTpmSts(&status);
	if (((status >> tpmFamilyShift) & tpmFamilyMask) != tpmFamilyTPM2) {
		fprintf(stderr, "unexpected TPM family value, status: %#x\n",
			status);
		return false;
	}
	FtdiReadReg(TPM_RID_REG, sizeof(cmd), &cmd);
	printf("Connected to device vid:did:rid of %4.4x:%4.4x:%2.2x\n",
	       did_vid & 0xffff, did_vid >> 16, cmd);

	GetVersion();

	return true;
}

/* This is in seconds (prime generation may take several minutes). */
#define MAX_STATUS_TIMEOUT 900
static int WaitForStatus(uint32_t statusMask, uint32_t statusExpected)
{
	uint32_t status;
	time_t target_time;
	static unsigned max_timeout;

	target_time = time(NULL) + MAX_STATUS_TIMEOUT;
	do {
		usleep(10000);
		if (time(NULL) >= target_time) {
			fprintf(stderr, "failed to get expected status %x\n",
				statusExpected);
			return false;
		}
		ReadTpmSts(&status);
	} while ((status & statusMask) != statusExpected);

	/* Calculate time spent waiting */
	target_time = MAX_STATUS_TIMEOUT - target_time + time(NULL);
	if (max_timeout < (unsigned)target_time) {
		max_timeout = target_time;
		printf("New max timeout: %d s\n", max_timeout);
	}

	return true;
}

static void SpinSpinner(void)
{
	static const char *spinner = "\\|/-";
	static int index;

	if (index > strlen(spinner))
		index = 0;

	fprintf(stdout, "%c[1D%c", 0x1b, spinner[index++]);
	fflush(stdout);
}

#define MAX_RESPONSE_SIZE 4096
#define HEADER_SIZE 6

/* tpm_command points at a buffer 4096 bytes in size */
struct swig_string_data FtdiSendCommandAndWait(char *tpm_command,
					       int command_size)
{
	uint32_t status;
	uint32_t expected_status_bits;
	size_t handled_so_far;
	uint32_t payload_size;
	char message[100];
	int offset = 0;
	uint8_t *response;

	if (!mpsse_) {
		fprintf(stderr, "attempt to use an uninitialized FTDI TPM!\n");
		return empty_string_data;
	}

	response = malloc(MAX_RESPONSE_SIZE);
	if (!response) {
		fprintf(stderr, "attempt to use an uninitialized FTDI TPM!\n");
		return empty_string_data;
	}

	handled_so_far = 0;

	WriteTpmSts(commandReady);

	memcpy(&payload_size, tpm_command + 2, sizeof(payload_size));
	payload_size = be32toh(payload_size);
	offset +=
	    snprintf(message, sizeof(message), "Message size %d", payload_size);

	/*
	 * No need to wait for the sts.Expect bit to be set, at least with the
	 * 15d1:001b and 1ae0:0028 devices. Let's just write the command into
	 * FIFO, make sure not to exceed the burst count.
	 */
	do {
		uint32_t transaction_size;
		uint32_t burst_count = GetBurstCount();

		if (burst_count > 64)
			burst_count = 64;

		transaction_size = command_size - handled_so_far;
		if (transaction_size > burst_count)
			transaction_size = burst_count;

		if (transaction_size) {
			FtdiWriteReg(TPM_DATA_FIFO_REG, transaction_size,
				     tpm_command + handled_so_far);
			handled_so_far += transaction_size;
		}
	} while (handled_so_far != command_size);

	/* And tell the device it can start processing it. */
	WriteTpmSts(tpmGo);

	expected_status_bits = stsValid | dataAvail;
	if (!WaitForStatus(expected_status_bits, expected_status_bits)) {
		size_t i;

		printf("Failed processing. %s:", message);
		for (i = 0; i < command_size; i++) {
			if (!(i % 16))
				printf("\n");
			printf(" %2.2x", (uint8_t)tpm_command[i]);
		}
		printf("\n");
		return empty_string_data;
	}

	/*
	 * The tpm_command is ready, let's read it.
	 *
	 * First we read the FIFO payload header, to see how much data to
	 * expect. The header size is fixed to six bytes, the total payload
	 * size is stored in network order in the last four bytes of the
	 * header.
	 */
	FtdiReadReg(TPM_DATA_FIFO_REG, HEADER_SIZE, response);
	handled_so_far = HEADER_SIZE;

	/* Figure out the total payload size. */
	memcpy(&payload_size, response + 2, sizeof(payload_size));
	payload_size = be32toh(payload_size);

	if (ftdi_trace_enabled)
		printf("%s response size %d\n\n", message, payload_size);
	else
		SpinSpinner();

	if (payload_size > MAX_RESPONSE_SIZE)
		return empty_string_data;
	/*
	 * Let's read all but the last byte in the FIFO to make sure the
	 * status register is showing correct flow control bits: 'more data'
	 * until the last byte and then 'no more data' once the last byte is
	 * read.
	 */
	payload_size = payload_size - 1;
	do {
		uint32_t transaction_size;
		uint32_t burst_count = GetBurstCount();

		if (burst_count > 64)
			burst_count = 64;

		transaction_size = payload_size - handled_so_far;
		if (transaction_size > burst_count)
			transaction_size = burst_count;

		if (transaction_size) {
			FtdiReadReg(TPM_DATA_FIFO_REG, transaction_size,
				    response + handled_so_far);
			handled_so_far += transaction_size;
		}
	} while (handled_so_far != payload_size);

	/* Verify that there is still data to come. */
	ReadTpmSts(&status);
	if ((status & expected_status_bits) != expected_status_bits) {
		fprintf(stderr, "unexpected status %#x\n", status);
		return empty_string_data;
	}

	FtdiReadReg(TPM_DATA_FIFO_REG, 1, response + handled_so_far);

	/* Verify that 'data available' is not asseretd any more. */
	ReadTpmSts(&status);
	if ((status & expected_status_bits) != stsValid) {
		fprintf(stderr, "unexpected status %#x\n", status);
		return empty_string_data;
	}

	/* Move the TPM back to idle state. */
	WriteTpmSts(commandReady);

	handled_so_far++;

	return (struct swig_string_data) {
		.size = handled_so_far, .data = response};
}
