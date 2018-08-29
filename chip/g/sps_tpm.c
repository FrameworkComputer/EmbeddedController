/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "sps.h"
#include "system.h"
#include "tpm_registers.h"
#include "util.h"

/*
 * This implements the TCG's TPM SPI Hardware Protocol on the SPI bus, using
 * the Cr50 SPS (SPI slave) controller. This turns out to be very similar to
 * the EC host command protocol, which is itself similar to HDLC. All of those
 * protocols provide ways to identify data frames over transports that don't
 * provide them natively. That's the nice thing about standards: there are so
 * many to choose from.
 *
 * ANYWAY, The goal of the TPM protocol is to provide read and write access to
 * device registers over the SPI bus. It is defined as follows (note that the
 * master clocks the bus, but both master and slave transmit data
 * simultaneously).
 *
 * Each transaction starts with the master clocking the bus to transfer 4
 * bytes:
 *
 * The master sends 4 bytes:       [R/W+size-1] [Addr] [Addr] [Addr]
 * The slave also sends 4 bytes:       [xx]      [xx]   [xx]   [x?]
 *
 * Bytes sent by the master define the direction and size (1-64 bytes) of the
 * data transfer, and the address of the register to access.
 *
 * The final bit of the 4th slave response byte determines whether or not the
 * slave needs some extra time. If that bit is 1, the master can IMMEDIATELY
 * clock in (or out) the number of bytes it specified with the header byte 0.
 *
 * If the final bit of the 4th response byte is 0, the master clocks eight more
 * bits and looks again at the new received byte. It repeats this process
 * (clock 8 bits, look at last bit) as long as every eighth bit is 0.
 *
 * When the slave is ready to proceed with the data transfer, it returns a 1
 * for the final bit of the response byte, at which point the master has to
 * resume transferring valid data for write transactions or to start reading
 * bytes sent by the slave for read transactions.
 *
 * So here's what a 4-byte write of value of 0x11223344 to register 0xAABBCC
 * might look like:
 *
 *   xfer:  1  2  3  4  5  6  7  8  9 10 11
 *   MOSI: 03 aa bb cc xx xx xx 11 22 33 44
 *   MISO: xx xx xx x0 x0 x0 x1 xx xx xx xx
 *
 * Bit 0 of MISO xfer #4 is 0, indicating that the slave needs to stall. The
 * slave stalled for three bytes before it was ready to continue accepting the
 * input data from the master. The slave released the stall in xfer #7.
 *
 * Here's a 4-byte read from register 0xAABBCC:
 *
 *   xfer:  1  2  3  4  5  6  7  8  9 10 11
 *   MOSI: 83 aa bb cc xx xx xx xx xx xx xx
 *   MISO: xx xx xx x0 x0 x0 x1 11 22 33 44
 *
 * As before, the slave stalled the read for three bytes and indicated it was
 * done stalling at xfer #7.
 *
 * Note that the ONLY place where a stall can be initiated is the last bit of
 * the fourth MISO byte of the transaction. Once the stall is released,
 * there's no stopping the rest of the data transfer.
 */

#define TPM_STALL_ASSERT   0x00
#define TPM_STALL_DEASSERT 0x01

/* Locality 0 register address base */
#define TPM_LOCALITY_0_SPI_BASE 0x00d40000

/* Console output macros */
#define CPUTS(outstr) cputs(CC_TPM, outstr)
#define CPRINTS(format, args...) cprints(CC_TPM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_TPM, format, ## args)

/*
 * Incoming messages are collected here until they're ready to process. The
 * buffer will start with a four-byte header, followed by whatever data
 * is sent by the master (none for a read, 1 to 64 bytes for a write).
 */
#define RXBUF_MAX 512			/* chosen arbitrarily */
static uint8_t rxbuf[RXBUF_MAX];
static unsigned rxbuf_count;		/* num bytes received */
static uint32_t bytecount;		/* Num of payload bytes when writing. */
static uint32_t regaddr;		/* Address of register to read/write. */

/*
 * Outgoing messages are shoved in here. We need a TPM_STALL_DEASSERT byte to
 * mark the start of the data stream before the data itself.
 */
#define TXBUF_MAX 512				/* chosen arbitrarily */
static uint8_t txbuf[1 + TXBUF_MAX];

static enum sps_state {
	/* Receiving header */
	SPS_TPM_STATE_RECEIVING_HEADER,

	/* Receiving data. */
	SPS_TPM_STATE_RECEIVING_WRITE_DATA,

	/* Finished rx processing, waiting for SPI transaction to finish. */
	SPS_TPM_STATE_PONDERING,

	/* Something went wrong. */
	SPS_TPM_STATE_RX_BAD,
} sps_tpm_state;

/* Set initial conditions to get ready to receive a command. */
static void init_new_cycle(void)
{
	rxbuf_count = 0;
	sps_tpm_state = SPS_TPM_STATE_RECEIVING_HEADER;
	sps_tx_status(TPM_STALL_ASSERT);
	/* We're just waiting for a new command, so we could sleep. */
	delay_sleep_by(1 * SECOND);
	enable_sleep(SLEEP_MASK_SPI);
}

/* Extract R/W bit, register address, and data count from 4-byte header */
static int header_says_to_read(uint8_t *data, uint32_t *reg, uint32_t *count)
{
	uint32_t addr = data[1];		/* reg address is MSB first */
	addr = (addr << 8) + data[2];
	addr = (addr << 8) + data[3];
	*reg = addr;
	*count = (data[0] & 0x3f) + 1;		/* bits 5-0: 1 to 64 bytes */
	return !!(data[0] & 0x80);		/* bit 7: 1=read, 0=write */
}

/* actual RX FIFO handler (runs in interrupt context) */
static void process_rx_data(uint8_t *data, size_t data_size, int cs_deasserted)
{
	/* We're receiving some bytes, so don't sleep */
	disable_sleep(SLEEP_MASK_SPI);

	if ((rxbuf_count + data_size) > RXBUF_MAX) {
		CPRINTS("TPM SPI input overflow: %d + %d > %d in state %d",
			rxbuf_count, data_size, RXBUF_MAX, sps_tpm_state);
		sps_tx_status(TPM_STALL_DEASSERT);
		sps_tpm_state = SPS_TPM_STATE_RX_BAD;
		/* In this state, this function won't be called again until
		 * after the CS deasserts and we've prepared for a new
		 * transaction. */
		return;
	}
	memcpy(rxbuf + rxbuf_count, data, data_size);
	rxbuf_count += data_size;

	/* Okay, we have enough. Now what? */
	if (sps_tpm_state == SPS_TPM_STATE_RECEIVING_HEADER) {
		if (rxbuf_count < 4)
			return;	/* Header is 4 bytes in size. */

		/* Got the header. What's it say to do? */
		if (header_says_to_read(rxbuf, &regaddr, &bytecount)) {
			/* Send the stall deassert manually */
			txbuf[0] = TPM_STALL_DEASSERT;

			/* Copy the register contents into the TXFIFO */
			/* TODO: This is blindly assuming TXFIFO has enough
			 * room. What can we do if it doesn't? */
			tpm_register_get(regaddr - TPM_LOCALITY_0_SPI_BASE,
					 txbuf + 1, bytecount);
			sps_transmit(txbuf, bytecount + 1);
			sps_tpm_state = SPS_TPM_STATE_PONDERING;
			return;
		}

		/*
		 * Write the new idle byte value, to signal the master to
		 * proceed with data.
		 */
		sps_tx_status(TPM_STALL_DEASSERT);
		sps_tpm_state = SPS_TPM_STATE_RECEIVING_WRITE_DATA;
		return;
	}

	if (cs_deasserted &&
	    (sps_tpm_state == SPS_TPM_STATE_RECEIVING_WRITE_DATA))
		/* Ok, we have all the write data, pass it to the tpm. */
		tpm_register_put(regaddr - TPM_LOCALITY_0_SPI_BASE,
				 rxbuf + rxbuf_count - bytecount, bytecount);
}

static void tpm_rx_handler(uint8_t *data, size_t data_size, int cs_deasserted)
{
	if (chip_factory_mode())
		return;  /* Ignore TPM traffic in factory mode. */

	if ((sps_tpm_state == SPS_TPM_STATE_RECEIVING_HEADER) ||
	    (sps_tpm_state == SPS_TPM_STATE_RECEIVING_WRITE_DATA))
		process_rx_data(data, data_size, cs_deasserted);

	if (cs_deasserted)
		init_new_cycle();
}

static void sps_if_stop(void)
{
	/* Let's shut down the interface while TPM is being reset. */
	sps_register_rx_handler(0, NULL, 0);
}

static void sps_if_start(void)
{
	/*
	 * Threshold of 3 makes sure we get an interrupt as soon as the header
	 * is received.
	 */
	init_new_cycle();
	sps_register_rx_handler(SPS_GENERIC_MODE, tpm_rx_handler, 3);
}


static void sps_if_register(void)
{
	if (!board_tpm_uses_spi())
		return;

	tpm_register_interface(sps_if_start, sps_if_stop);
}
DECLARE_HOOK(HOOK_INIT, sps_if_register, HOOK_PRIO_LAST);
