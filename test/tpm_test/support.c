/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Based on Craig Heffner's version of Dec 27 2011, published on
 * https://github.com/devttys0/libmpsse
 *
 * Internal functions used by libmpsse.
 */

#include <string.h>
#include <stdlib.h>

#if LIBFTDI1 == 1
#include <libftdi1/ftdi.h>
#else
#include <ftdi.h>
#endif

#include "support.h"

/* Write data to the FTDI chip */
int raw_write(struct mpsse_context *mpsse, unsigned char *buf, int size)
{
	int retval = MPSSE_FAIL;

	if (mpsse->mode && (ftdi_write_data(&mpsse->ftdi, buf, size) == size))
		retval = MPSSE_OK;

	return retval;
}

/* Read data from the FTDI chip */
int raw_read(struct mpsse_context *mpsse, unsigned char *buf, int size)
{
	int n = 0, r = 0;

	if (!mpsse->mode)
		return 0;

	while (n < size) {
		r = ftdi_read_data(&mpsse->ftdi, buf, size);
		if (r < 0)
			break;
		n += r;
	}

	if (mpsse->flush_after_read) {
		/*
		 * Make sure the buffers are cleared after a read or
		 * subsequent reads may fail. Is this needed anymore?
		 * It slows down repetitive read operations by ~8%.
		 */
		ftdi_usb_purge_rx_buffer(&mpsse->ftdi);
	}

	return n;
}

/* Sets the read and write timeout periods for bulk usb data transfers. */
void set_timeouts(struct mpsse_context *mpsse, int timeout)
{
	if (mpsse->mode) {
		mpsse->ftdi.usb_read_timeout = timeout;
		mpsse->ftdi.usb_write_timeout = timeout;
	}
}

/* Convert a frequency to a clock divisor */
uint16_t freq2div(uint32_t system_clock, uint32_t freq)
{
	return (((system_clock / freq) / 2) - 1);
}

/* Convert a clock divisor to a frequency */
uint32_t div2freq(uint32_t system_clock, uint16_t div)
{
	return (system_clock / ((1 + div) * 2));
}

/* Builds a buffer of commands + data blocks */
unsigned char *build_block_buffer(struct mpsse_context *mpsse,
				  uint8_t cmd,
				  unsigned char *data, int size, int *buf_size)
{
	unsigned char *buf = NULL;
	int i = 0, j = 0, k = 0, dsize = 0, num_blocks = 0, total_size =
	    0, xfer_size = 0;
	uint16_t rsize = 0;

	*buf_size = 0;

	/* Data block size is 1 in I2C, or when in bitmode */
	if (mpsse->mode == I2C || (cmd & MPSSE_BITMODE))
		xfer_size = 1;
	else
		xfer_size = mpsse->xsize;

	num_blocks = (size / xfer_size);
	if (size % xfer_size)
		num_blocks++;

	/*
	 * The total size of the data will be the data size + the write
	 * command
	 */
	total_size = size + (CMD_SIZE * num_blocks);

	buf = malloc(total_size);
	if (!buf)
		return NULL;

	memset(buf, 0, total_size);

	for (j = 0; j < num_blocks; j++) {
		dsize = size - k;
		if (dsize > xfer_size)
			dsize = xfer_size;

		/* The reported size of this block is block size - 1 */
		rsize = dsize - 1;

		/* Copy in the command for this block */
		buf[i++] = cmd;
		buf[i++] = (rsize & 0xFF);
		if (!(cmd & MPSSE_BITMODE))
			buf[i++] = ((rsize >> 8) & 0xFF);

		/* On a write, copy the data to transmit after the command */
		if (cmd == mpsse->tx || cmd == mpsse->txrx) {

			memcpy(buf + i, data + k, dsize);

			/* i == offset into buf */
			i += dsize;
			/* k == offset into data */
			k += dsize;
		}
	}

	*buf_size = i;

	return buf;
}

/* Set the low bit pins high/low */
int set_bits_low(struct mpsse_context *mpsse, int port)
{
	char buf[CMD_SIZE] = { 0 };

	buf[0] = SET_BITS_LOW;
	buf[1] = port;
	buf[2] = mpsse->tris;

	return raw_write(mpsse, (unsigned char *)&buf, sizeof(buf));
}

/* Set the high bit pins high/low */
int set_bits_high(struct mpsse_context *mpsse, int port)
{
	char buf[CMD_SIZE] = { 0 };

	buf[0] = SET_BITS_HIGH;
	buf[1] = port;
	buf[2] = mpsse->trish;

	return raw_write(mpsse, (unsigned char *)&buf, sizeof(buf));
}

/* Set the GPIO pins high/low */
int gpio_write(struct mpsse_context *mpsse, int pin, int direction)
{
	int retval = MPSSE_FAIL;

	/*
	 * The first four pins can't be changed unless we are in a stopped
	 * status
	 */
	if (pin < NUM_GPIOL_PINS && mpsse->status == STOPPED) {
		/* Convert pin number (0-3) to the corresponding pin bit */
		pin = (GPIO0 << pin);

		if (direction == HIGH) {
			mpsse->pstart |= pin;
			mpsse->pidle |= pin;
			mpsse->pstop |= pin;
		} else {
			mpsse->pstart &= ~pin;
			mpsse->pidle &= ~pin;
			mpsse->pstop &= ~pin;
		}

		retval = set_bits_low(mpsse, mpsse->pstop);
	} else if (pin >= NUM_GPIOL_PINS && pin < NUM_GPIO_PINS) {
		/* Convert pin number (4 - 11) to the corresponding pin bit */
		pin -= NUM_GPIOL_PINS;

		if (direction == HIGH)
			mpsse->gpioh |= (1 << pin);
		else
			mpsse->gpioh &= ~(1 << pin);

		retval = set_bits_high(mpsse, mpsse->gpioh);
	}

	return retval;
}

/* Checks if a given MPSSE context is valid. */
int is_valid_context(struct mpsse_context *mpsse)
{
	int retval = 0;

	if (mpsse != NULL && mpsse->open)
		retval = 1;

	return retval;
}
