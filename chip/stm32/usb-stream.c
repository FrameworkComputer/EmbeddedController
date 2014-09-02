/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "common.h"
#include "config.h"
#include "link_defs.h"
#include "printf.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb.h"
#include "usb-stream.h"

/*
 * The USB packet RAM is attached to the processor via the AHB2APB bridge.  This
 * bridge performs manipulations of read and write accesses as per the note in
 * section 2.1 of RM0091.  The upshot is that it is OK to read from the packet
 * RAM using 8-bit or 16-bit accesses, but not 32-bit, and it is only really OK
 * to write to the packet RAM using 16-bit accesses.  Thus custom memcpy like
 * routines need to be employed.  Furthermore, reading from and writing to the
 * RX and TX queues uses memcpy, which will try to do 32-bit accesses if it can.
 * so we must read and write single bytes at a time and construct 16-bit
 * accesses to the packet RAM.
 *
 * This could be improved by adding a set of operations on the queue to get
 * a pointer and size of the largest contiguous free/full region, then that
 * region could be operated on and a commit operation could be performed on
 * the queue.
 */
static size_t rx_read(struct usb_stream_config const *config)
{
	size_t count = btable_ep[config->endpoint].rx_count & 0x3ff;

	if (count < queue_space(&config->rx)) {
		usb_uint *buffer = config->rx_ram;
		size_t    i;

		for (i = 0; i < count / 2; i++) {
			usb_uint word = *buffer++;
			uint8_t  lsb  = (word >> 0) & 0xff;
			uint8_t  msb  = (word >> 8) & 0xff;

			queue_add_unit(&config->rx, &lsb);
			queue_add_unit(&config->rx, &msb);
		}

		if (count & 1) {
			usb_uint word = *buffer++;
			uint8_t  lsb  = (word >> 0) & 0xff;

			queue_add_unit(&config->rx, &lsb);
		}

		return count;
	}

	return 0;
}

static size_t tx_write(struct usb_stream_config const *config)
{
	usb_uint *buffer = config->tx_ram;
	size_t    count  = MIN(USB_MAX_PACKET_SIZE, queue_count(&config->tx));
	size_t    i;

	for (i = 0; i < count / 2; i++) {
		uint8_t lsb;
		uint8_t msb;

		queue_remove_unit(&config->tx, &lsb);
		queue_remove_unit(&config->tx, &msb);

		*buffer++ = (msb << 8) | lsb;
	}

	if (count & 1) {
		uint8_t lsb;

		queue_remove_unit(&config->tx, &lsb);

		*buffer++ = lsb;
	}

	btable_ep[config->endpoint].tx_count = count;

	return count;
}

static size_t usb_read(struct in_stream const *stream,
		       uint8_t *buffer,
		       size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(stream, struct usb_stream_config, in);

	size_t read = QUEUE_REMOVE_UNITS(&config->rx, buffer, count);

	if (config->state->rx_waiting && rx_read(config)) {
		config->state->rx_waiting = 0;

		STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);

		/*
		 * Make sure that the reader of this queue knows that there is
		 * more to read.
		 */
		in_stream_ready(&config->in);

		/*
		 * If there is still space left in the callers buffer fill it
		 * up with the additional bytes just added to the queue.
		 */
		if (count - read > 0)
			read += QUEUE_REMOVE_UNITS(&config->rx,
						   buffer + read,
						   count - read);
	}

	return read;
}

static int tx_valid(struct usb_stream_config const *config)
{
	return (STM32_USB_EP(config->endpoint) & EP_TX_MASK) == EP_TX_VALID;
}

static size_t usb_write(struct out_stream const *stream,
			uint8_t const *buffer,
			size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(stream, struct usb_stream_config, out);

	size_t wrote = QUEUE_ADD_UNITS(&config->tx, buffer, count);

	/*
	 * If we are not currently in a valid transmission state and we had
	 * something for the TX buffer, then mark the TX endpoint as valid.
	 */
	if (!tx_valid(config) && tx_write(config))
		STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);

	return wrote;
}

static void usb_flush(struct out_stream const *stream)
{
	struct usb_stream_config const *config =
		DOWNCAST(stream, struct usb_stream_config, out);

	while (tx_valid(config) || queue_count(&config->tx))
		;
}

struct in_stream_ops const usb_stream_in_stream_ops = {
	.read = usb_read,
};

struct out_stream_ops const usb_stream_out_stream_ops = {
	.write = usb_write,
	.flush = usb_flush,
};

void usb_stream_tx(struct usb_stream_config const *config)
{
	if (tx_write(config))
		STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);
	else
		STM32_TOGGLE_EP(config->endpoint, 0, 0, 0);

	out_stream_ready(&config->out);
}

void usb_stream_rx(struct usb_stream_config const *config)
{
	if (rx_read(config)) {
		/*
		 * RX packet consumed, mark the packet as VALID.
		 */
		STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);
	} else {
		/*
		 * There is not enough space in the RX queue to receive this
		 * packet.  Leave the RX endpoint in a NAK state, clear the
		 * interrupt, and indicate to the usb_read function that when
		 * there is enough space in the queue to hold it there is an
		 * RX packet waiting.
		 */
		config->state->rx_waiting = 1;
		STM32_TOGGLE_EP(config->endpoint, 0, 0, 0);
	}

	in_stream_ready(&config->in);
}

void usb_stream_reset(struct usb_stream_config const *config)
{
	int i = config->endpoint;

	btable_ep[i].tx_addr  = usb_sram_addr(config->tx_ram);
	btable_ep[i].tx_count = 0;

	btable_ep[i].rx_addr  = usb_sram_addr(config->rx_ram);
	btable_ep[i].rx_count = 0x8000 | ((USB_MAX_PACKET_SIZE / 32 - 1) << 10);

	config->state->rx_waiting = 0;

	STM32_USB_EP(i) = ((i <<  0) | /* Endpoint Addr*/
			   (2 <<  4) | /* TX NAK */
			   (0 <<  9) | /* Bulk EP */
			   (3 << 12)); /* RX VALID */
}
