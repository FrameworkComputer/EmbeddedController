/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"
#include "timer.h"
#include "usb_dwc_stream.h"
#include "util.h"

#include "console.h"
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

/*
 * This function tries to shove new bytes from the USB host into the queue for
 * consumption elsewhere. It is invoked either by a HW interrupt (telling us we
 * have new bytes from the USB host), or by whoever is reading bytes out of the
 * other end of the queue (telling us that there's now more room in the queue
 * if we still have bytes to shove in there).
 */
int rx_stream_handler(struct usb_stream_config const *config)
{
	int rx_count = rx_ep_pending(config->endpoint);

	/* If we have some, try to shove them into the queue */
	if (rx_count) {
		size_t added = QUEUE_ADD_UNITS(
			config->producer.queue, config->rx_ram,
			rx_count);
		if (added != rx_count) {
			CPRINTF("rx_stream_handler: failed ep%d "
				"queue %d bytes, accepted %d\n",
				config->endpoint, rx_count, added);
		}
	}

	if (!rx_ep_is_active(config->endpoint))
		usb_read_ep(config->endpoint, config->rx_size, config->rx_ram);

	return rx_count;
}

/* Try to send some bytes to the host */
int tx_stream_handler(struct usb_stream_config const *config)
{
	size_t count;

	if (!*(config->is_reset))
		return 0;
	if (!tx_ep_is_ready(config->endpoint))
		return 0;

	count = QUEUE_REMOVE_UNITS(config->consumer.queue, config->tx_ram,
				   config->tx_size);
	if (count)
		usb_write_ep(config->endpoint, count, config->tx_ram);

	return count;
}

/* Reset stream */
void usb_stream_event(struct usb_stream_config const *config,
		enum usb_ep_event evt)
{
	if (evt != USB_EVENT_RESET)
		return;

	epN_reset(config->endpoint);

	*(config->is_reset) = 1;

	/* Flush any queued data */
	hook_call_deferred(config->deferred_tx, 0);
	hook_call_deferred(config->deferred_rx, 0);
}

static void usb_read(struct producer const *producer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(producer, struct usb_stream_config, producer);

	hook_call_deferred(config->deferred_rx, 0);
}

static void usb_written(struct consumer const *consumer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(consumer, struct usb_stream_config, consumer);

	hook_call_deferred(config->deferred_tx, 0);
}

struct producer_ops const usb_stream_producer_ops = {
	.read = usb_read,
};

struct consumer_ops const usb_stream_consumer_ops = {
	.written = usb_written,
};
