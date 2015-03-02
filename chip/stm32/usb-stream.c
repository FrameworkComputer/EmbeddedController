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

static size_t rx_read(struct usb_stream_config const *config)
{
	size_t count = btable_ep[config->endpoint].rx_count & 0x3ff;

	/*
	 * Only read the received USB packet if there is enough space in the
	 * receive queue.
	 */
	if (count >= queue_space(config->producer.queue))
		return 0;

	return producer_write_memcpy(&config->producer,
				     config->rx_ram,
				     count,
				     memcpy_from_usbram);
}

static size_t tx_write(struct usb_stream_config const *config)
{
	size_t count = consumer_read_memcpy(&config->consumer,
					    config->tx_ram,
					    config->tx_size,
					    memcpy_to_usbram);

	btable_ep[config->endpoint].tx_count = count;

	return count;
}

static void usb_read(struct producer const *producer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(producer, struct usb_stream_config, producer);

	if (config->state->rx_waiting && rx_read(config)) {
		config->state->rx_waiting = 0;

		STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);
	}
}

static int tx_valid(struct usb_stream_config const *config)
{
	return (STM32_USB_EP(config->endpoint) & EP_TX_MASK) == EP_TX_VALID;
}

static void usb_written(struct consumer const *consumer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(consumer, struct usb_stream_config, consumer);

	/*
	 * If we are not currently in a valid transmission state and we had
	 * something for the TX buffer, then mark the TX endpoint as valid.
	 */
	if (!tx_valid(config) && tx_write(config))
		STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);
}

static void usb_flush(struct consumer const *consumer)
{
	struct usb_stream_config const *config =
		DOWNCAST(consumer, struct usb_stream_config, consumer);

	while (tx_valid(config) || queue_count(consumer->queue))
		;
}

struct producer_ops const usb_stream_producer_ops = {
	.read = usb_read,
};

struct consumer_ops const usb_stream_consumer_ops = {
	.written = usb_written,
	.flush   = usb_flush,
};

void usb_stream_tx(struct usb_stream_config const *config)
{
	if (tx_write(config))
		STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);
	else
		STM32_TOGGLE_EP(config->endpoint, 0, 0, 0);
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
}

static usb_uint usb_ep_rx_size(size_t bytes)
{
	if (bytes < 64)
		return bytes << 9;
	else
		return 0x8000 | ((bytes - 32) << 5);
}

void usb_stream_reset(struct usb_stream_config const *config)
{
	int i = config->endpoint;

	btable_ep[i].tx_addr  = usb_sram_addr(config->tx_ram);
	btable_ep[i].tx_count = 0;

	btable_ep[i].rx_addr  = usb_sram_addr(config->rx_ram);
	btable_ep[i].rx_count = usb_ep_rx_size(config->rx_size);

	config->state->rx_waiting = 0;

	STM32_USB_EP(i) = ((i <<  0) | /* Endpoint Addr*/
			   (2 <<  4) | /* TX NAK */
			   (0 <<  9) | /* Bulk EP */
			   (3 << 12)); /* RX VALID */
}
