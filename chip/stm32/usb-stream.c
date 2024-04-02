/* Copyright 2014 The ChromiumOS Authors
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
#include "usart.h"
#include "usb-stream.h"
#include "usb_hw.h"
#include "util.h"

static size_t rx_read(struct usb_stream_config const *config)
{
	uintptr_t address = btable_ep[config->endpoint].rx_addr;
	size_t count = btable_ep[config->endpoint].rx_count & RX_COUNT_MASK;

	/*
	 * Only read the received USB packet if there is enough space in the
	 * receive queue.
	 */
	if (count > queue_space(config->producer.queue))
		return 0;

	return queue_add_memcpy(config->producer.queue, (void *)address, count,
				memcpy_from_usbram);
}

static size_t tx_write(struct usb_stream_config const *config)
{
	uintptr_t address = btable_ep[config->endpoint].tx_addr;
	size_t count = queue_remove_memcpy(config->consumer.queue,
					   (void *)address, config->tx_size,
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

struct producer_ops const usb_stream_producer_ops = {
	.read = usb_read,
};

struct consumer_ops const usb_stream_consumer_ops = {
	.written = usb_written,
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

void usb_stream_event(struct usb_stream_config const *config,
		      enum usb_ep_event evt)
{
	int i;

	if (evt != USB_EVENT_RESET)
		return;

	i = config->endpoint;

	btable_ep[i].tx_addr = usb_sram_addr(config->tx_ram);
	btable_ep[i].tx_count = 0;

	btable_ep[i].rx_addr = usb_sram_addr(config->rx_ram);
	btable_ep[i].rx_count = usb_ep_rx_size(config->rx_size);

	config->state->rx_waiting = 0;

	STM32_USB_EP(i) = ((i << 0) | /* Endpoint Addr*/
			   (tx_write(config) ? EP_TX_VALID : EP_TX_NAK) |
			   (0 << 9) | /* Bulk EP */
			   (rx_disabled(config) ? EP_RX_NAK : EP_RX_VALID));
}

int usb_usart_interface(struct usb_stream_config const *config,
			struct usart_config const *usart, int interface,
			usb_uint *rx_buf, usb_uint *tx_buf)
{
	struct usb_setup_packet req;

	usb_read_setup_packet(rx_buf, &req);

	if (req.bmRequestType ==
	    (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE)) {
		uint16_t response;
		switch (req.bRequest) {
		/* Get current parity setting. */
		case USB_USART_REQ_PARITY:
			response = usart_get_parity(usart);
			break;
		/* Get current baud rate. */
		case USB_USART_REQ_BAUD:
			response = DIV_ROUND_NEAREST(usart_get_baud(usart) + 50,
						     100);
			break;
		default:
			return -1;
		}
		memcpy_to_usbram((void *)usb_sram_addr(tx_buf), &response,
				 sizeof(response));
		btable_ep[0].tx_count = sizeof(response);
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
		return 0;
	}

	if (req.bmRequestType !=
	    (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE))
		return -1;

	if (req.wIndex != interface || req.wLength != 0)
		return -1;

	switch (req.bRequest) {
	/* Set parity. */
	case USB_USART_SET_PARITY:
		usart_set_parity(usart, req.wValue);
		break;
	/* Set baud rate. */
	case USB_USART_SET_BAUD:
		usart_set_baud(usart, req.wValue * 100);
		break;
	/* Start or end break condition on the TX wire. */
	case USB_USART_BREAK:
		if (req.wValue == 0xFFFF) {
			/* Start indefinite break condition. */
			usart_set_break(usart, true);
		} else if (req.wValue == 0) {
			/* End break condition. */
			usart_set_break(usart, false);
		} else {
			/*
			 * Other values reserved for future support for pulse
			 * of particular length.
			 */
			return -1;
		}
		break;
	default:
		return -1;
	}

	btable_ep[0].tx_count = 0;
	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, EP_STATUS_OUT);
	return 0;
}
