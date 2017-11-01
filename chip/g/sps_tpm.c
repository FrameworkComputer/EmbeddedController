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
#define RXBUF_MAX 512				/* chosen arbitrarily */
static uint8_t rxbuf[RXBUF_MAX];
static unsigned rxbuf_count;		/* num bytes received */
static unsigned rxbuf_needed;		/* num bytes we'd like */
static unsigned rx_fifo_base;		/* RX fifo at transaction start. */
static unsigned stall_threshold;	/* num bytes we'd like */
static uint32_t bytecount;
static uint32_t regaddr;


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
	rxbuf_needed = 4;
	sps_tpm_state = SPS_TPM_STATE_RECEIVING_HEADER;
	rx_fifo_base = sps_rx_fifo_wrptr();
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
static void process_rx_data(uint8_t *data, size_t data_size)
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

	/* Wait until we have enough. */
	if (rxbuf_count < rxbuf_needed)
		return;

	/* Okay, we have enough. Now what? */
	if (sps_tpm_state == SPS_TPM_STATE_RECEIVING_HEADER) {
		uint32_t old_wrptr, wrptr;

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
		 * Master is writing, we will need more data.
		 *
		 * This is a tricky part, as we do not know how many dummy
		 * bytes the master has already written. And the actual data
		 * of course will start arriving only after we change the idle
		 * byte to set the LSB to 1.
		 *
		 * What we do know is that the idle byte repeatedly sent on
		 * the MISO line is sampled at the same time as the RX FIFO
		 * write pointer is written by the chip, after clocking in the
		 * next byte. That is, we can synchronize with the line by
		 * waiting for the RX FIFO write pointer to change. Then we
		 * can change the idle byte to indicate that the slave is
		 * ready to receive the rest of the data, and take note of the
		 * RX FIFO write pointer, as the first byte of the message
		 * will show up in the receive FIFO 2 bytes later.
		 */

		/*
		 * Let's wait til the start of the next byte cycle. This must
		 * be done in a tight loop (with interrupts disabled?).
		 */
		old_wrptr = sps_rx_fifo_wrptr();
		do {
			wrptr = sps_rx_fifo_wrptr();
		} while (old_wrptr == wrptr);

		/*
		 * Write the new idle byte value, it will start transmitting
		 * *next* after the current byte.
		 */
		sps_tx_status(TPM_STALL_DEASSERT);

		/*
		 * Verify that we managed to change the idle byte value within
		 * the required time (RX FIFO write pointer has not changed)
		 */
		if (sps_rx_fifo_wrptr() != wrptr) {
			CPRINTS("%s:"
				" ERROR: failed to change idle byte in time",
				__func__);
			sps_tpm_state = SPS_TPM_STATE_PONDERING;
		} else {
			/*
			 * Ok, we're good. Remember where in the receive
			 * stream the actual data will start showing up. It is
			 * two bytes after the current one (the current idle
			 * byte still has the LSB set to zero, the next one
			 * will have the LSB set to one, only after receiving
			 * it the master will start sending the actual data.
			 */
			stall_threshold =
				((wrptr - rx_fifo_base) & SPS_FIFO_MASK) + 2;
			rxbuf_needed = stall_threshold + bytecount;
			sps_tpm_state = SPS_TPM_STATE_RECEIVING_WRITE_DATA;
		}
		return;
	}

	if (sps_tpm_state == SPS_TPM_STATE_RECEIVING_WRITE_DATA) {
		/* Ok, we have all the write data. */
		tpm_register_put(regaddr - TPM_LOCALITY_0_SPI_BASE,
				 rxbuf + stall_threshold, bytecount);
		sps_tpm_state = SPS_TPM_STATE_PONDERING;
	}
}

static void tpm_rx_handler(uint8_t *data, size_t data_size, int cs_disabled)
{
	if (chip_factory_mode())
		return;  /* Ignore TPM traffic in factory mode. */

	if ((sps_tpm_state == SPS_TPM_STATE_RECEIVING_HEADER) ||
	    (sps_tpm_state == SPS_TPM_STATE_RECEIVING_WRITE_DATA))
		process_rx_data(data, data_size);

	if (cs_disabled)
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
