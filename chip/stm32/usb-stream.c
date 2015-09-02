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
	uintptr_t address = btable_ep[config->endpoint].rx_addr;
	size_t    count   = btable_ep[config->endpoint].rx_count & 0x3ff;

	/*
	 * Only read the received USB packet if there is enough space in the
	 * receive queue.
	 */
	if (count >= queue_space(config->producer.queue))
		return 0;

	return queue_add_memcpy(config->producer.queue,
				(void *) address,
				count,
				memcpy_from_usbram);
}

static size_t tx_write(struct usb_stream_config const *config)
{
	uintptr_t address = btable_ep[config->endpoint].tx_addr;
	size_t    count   = queue_remove_memcpy(config->consumer.queue,
						(void *) address,
						config->tx_size,
						memcpy_to_usbram);

	btable_ep[config->endpoint].tx_count = count;

	return count;
}

static int tx_valid(struct usb_stream_config const *config)
{
	return (STM32_USB_EP(config->endpoint) & EP_TX_MASK) == EP_TX_VALID;
}

static int rx_valid(struct usb_stream_config const *config)
{
	return (STM32_USB_EP(config->endpoint) & EP_RX_MASK) == EP_RX_VALID;
}

static int rx_disabled(struct usb_stream_config const *config)
{
	return config->state->rx_disabled;
}

static void usb_read(struct producer const *producer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(producer, struct usb_stream_config, producer);

	hook_call_deferred(config->deferred, 0);
}

static void usb_written(struct consumer const *consumer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(consumer, struct usb_stream_config, consumer);

	hook_call_deferred(config->deferred, 0);
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

void usb_stream_deferred(struct usb_stream_config const *config)
{
	if (!tx_valid(config) && tx_write(config))
		STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);

	if (!rx_valid(config) && !rx_disabled(config) && rx_read(config))
		STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);
}

void usb_stream_tx(struct usb_stream_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, 0, 0, 0);

	hook_call_deferred(config->deferred, 0);
}

void usb_stream_rx(struct usb_stream_config const *config)
{
	STM32_TOGGLE_EP(config->endpoint, 0, 0, 0);

	hook_call_deferred(config->deferred, 0);
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
			   (rx_disabled(config) ? EP_RX_NAK : EP_RX_VALID));
}
