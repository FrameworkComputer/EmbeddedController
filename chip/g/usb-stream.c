/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb-stream.h"

/* Let the USB HW IN-to-host FIFO transmit some bytes */
static void usb_enable_tx(struct usb_stream_config const *config,
			  const int len[])
{
	const uint32_t flags = DIEPDMA_BS_HOST_RDY | DIEPDMA_IOC | DIEPDMA_LAST;
	int idx = 0;

	if (len[1]) {
		config->in_desc[idx].flags = DIEPDMA_TXBYTES(len[idx]) |
					     DIEPDMA_BS_HOST_RDY;
		idx++;
	}
	config->in_desc[idx].flags = DIEPDMA_TXBYTES(len[idx]) | flags;

	GR_USB_DIEPCTL(config->endpoint) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
}

/* Let the USB HW OUT-from-host FIFO receive some bytes */
static void usb_enable_rx(struct usb_stream_config const *config, int len)
{
	config->out_desc->flags = DOEPDMA_RXBYTES(len) | DOEPDMA_LAST |
				  DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
	GR_USB_DOEPCTL(config->endpoint) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
}

/* True if the HW Rx/OUT FIFO has bytes for us. */
static inline int rx_fifo_is_ready(struct usb_stream_config const *config)
{
	return (config->out_desc->flags & DOEPDMA_BS_MASK) ==
	       DOEPDMA_BS_DMA_DONE;
}

/*
 * This function tries to shove new bytes from the USB host into the queue for
 * consumption elsewhere. It is invoked either by a HW interrupt (telling us we
 * have new bytes from the USB host), or by whoever is reading bytes out of the
 * other end of the queue (telling us that there's now more room in the queue
 * if we still have bytes to shove in there).
 */
void rx_stream_handler(struct usb_stream_config const *config)
{
	/*
	 * The HW FIFO buffer (rx_ram) is always filled from [0] by the
	 * hardware. The rx_in_fifo variable counts how many bytes of that
	 * buffer are actually valid, and is calculated from the HW DMA
	 * descriptor table. The descriptor is updated by the hardware, and it
	 * and rx_ram remains valid and unchanged until software tells the
	 * the hardware engine to accept more input.
	 */
	int rx_in_fifo, rx_left;

	/*
	 * The rx_handled variable tracks how many of the bytes in the HW FIFO
	 * we've copied into the incoming queue. The queue may not accept all
	 * of them at once, so we have to keep track of where we are so that
	 * the next time this function is called we can try to shove the rest
	 * of the HW FIFO bytes into the queue.
	 */
	int rx_handled;

	/* If the HW FIFO isn't ready, then we're waiting for more bytes */
	if (!rx_fifo_is_ready(config))
		return;

	rx_handled = *(config->rx_handled);
	/*
	 * How many of the HW FIFO bytes have we not yet handled? We need to
	 * know both where we are in the buffer and how many bytes we haven't
	 * yet enqueued. One can be calculated from the other as long as we
	 * know rx_in_fifo, but we need at least one static variable.
	 */
	rx_in_fifo = config->rx_size
		- (config->out_desc->flags & DOEPDMA_RXBYTES_MASK);
	rx_left = rx_in_fifo - rx_handled;

	/* If we have some, try to shove them into the queue */
	if (rx_left) {
		size_t added = QUEUE_ADD_UNITS(
			config->producer.queue, config->rx_ram + rx_handled,
			rx_left);
		rx_handled += added;
		rx_left -= added;
	}

	/*
	 * When we've handled all the bytes in the queue ("rx_in_fifo ==
	 * rx_handled" and "rx_left == 0" indicate the same thing), we can
	 * reenable the USB HW to go fetch more.
	 */
	if (!rx_left) {
		rx_handled = 0;
		usb_enable_rx(config, config->rx_size);
	} else {
		hook_call_deferred(config->deferred_rx, 0);
	}

	*(config->rx_handled) = rx_handled;
}

/* Rx/OUT interrupt handler */
void usb_stream_rx(struct usb_stream_config const *config)
{
	/* Wake up the Rx FIFO handler */
	hook_call_deferred(config->deferred_rx, 0);

	GR_USB_DOEPINT(config->endpoint) = 0xffffffff;
}

/* True if the Tx/IN FIFO can take some bytes from us. */
int tx_fifo_is_ready(struct usb_stream_config const *config)
{
	uint32_t status;
	struct g_usb_desc *in_desc = config->in_desc;

	if (!(in_desc->flags & DIEPDMA_LAST))
		++in_desc;

	status = in_desc->flags & DIEPDMA_BS_MASK;
	return status == DIEPDMA_BS_DMA_DONE || status == DIEPDMA_BS_HOST_BSY;
}

/* Try to send some bytes to the host */
static void tx_stream_handler(struct usb_stream_config const *config)
{
	int len[MAX_IN_DESC];
	size_t count;
	size_t head;
	struct queue const *tx_q = config->consumer.queue;

	/* setup to send bytes to the host */
	count = MIN(queue_count(tx_q), config->tx_size);
	if (!count) {
		/* Report USB TX transfer is not active any more. */
		*config->tx_in_progress = 0;
		return;
	}

	head = tx_q->state->head & tx_q->buffer_units_mask;

	if (config->is_uart_console) {
		if (!*config->kicker_running &&
		    (count < config->tx_size)) {
		/*
		 * Shipping less than full chunk (64 bytes) over usb is
		 * wasteful in case there is a lot of data coming from the
		 * stream source. Let's try collecting more bytes in case more
		 * is coming.
		 *
		 * It takes 5.6 ms to transfer 64 bytes over UART at 115200
		 * bps with one start and one stop bit. Let's set the deferred
		 * function delay to 3 ms, it will take longer in reality as
		 * background tasks will get a chance to run.
		 */
			hook_call_deferred(config->tx_kicker, 3 * MSEC);
			*config->kicker_running = 1;
			return;
		}

		if (*config->kicker_running) {
			*config->kicker_running = 0;
			hook_call_deferred(config->tx_kicker, -1);
		}
	}

	/*
	 * If queue units are not physically continuous, then setup transfer
	 * in two USB endpoint descriptors.
	 *
	 *      buffer                         buffer + buffer_units
	 *      |     tail                head |
	 *      |     |                   |    |
	 *      V     V                   V    V
	 * tx_q |xxxxxx___________________xxxxx|
	 *       <---->                   <--->
	 *      len[1]                    len[0]
	 */
	len[0] = MIN(count, tx_q->buffer_units - head);
	len[1] = count - len[0];

	/*
	 * Store the amount to advance head when the transfer is done.
	 * Note: 'tx byte' field in the endpoint descriptor decreases to zero
	 *       as data get transferred. Need to store the transfer size,
	 *       which is 'count', aside into *config-> tx_handlered.
	 */
	*(config->tx_handled) = count;

	/*
	 * Setup the first endpoint descriptor with start memory address No
	 * need to setup for the second endpoint, because it is always the
	 * start address of the queue, and already setup in
	 * usb_stream_reset().
	 */
	config->in_desc[0].addr = (void *)tx_q->buffer + head;

	/*
	 * Enable USB transfer. usb_enable_tx() will setup the transfer size
	 * in the first endpoint descriptor, and the second descriptor as well
	 * if it is needed.
	 */
	usb_enable_tx(config, len);
}

/*
 * Deferred function which gets to run if a UART console does not supply
 * enough data to fill a USB chunk (64 bytes).
 */
void tx_stream_kicker(struct usb_stream_config const *config)
{
	/*
	 * By design this function must run on a task context, i.e. interrupts
	 * are enabled.
	 *
	 * The not so elegant but simplest way to avoid concurrency issues
	 * with the kicker function execution interrupted by a USB or UART
	 * event is to invoke tx_stream_handler() with disabled interrupts.
	 */
	interrupt_disable();

	if (*config->kicker_running)
		tx_stream_handler(config);

	interrupt_enable();
}

/* Tx/IN interrupt handler */
void usb_stream_tx(struct usb_stream_config const *config)
{
	size_t *tx_handled;

	/* Clear the Tx/IN interrupts */
	GR_USB_DIEPINT(config->endpoint) = 0xffffffff;

	/* Address of the size of the most recent chunk. */
	tx_handled = config->tx_handled;

	/*
	 * Transfer completed, advance queue head by the number of bytes
	 * transmitted in the most recent chunk.
	 */
	queue_advance_head(config->consumer.queue, *tx_handled);

	*tx_handled = 0;

	/* See if there is more to transmit. */
	tx_stream_handler(config);
}

void usb_stream_reset(struct usb_stream_config const *config)
{
	/*
	 * Mark USB TX transfer is in progress, because it shall be so at
	 * the end of this function to flush any queued data.
	 */
	*config->tx_in_progress = 1;

	config->out_desc->flags = DOEPDMA_RXBYTES(config->rx_size) |
				  DOEPDMA_LAST | DOEPDMA_BS_HOST_RDY |
				  DOEPDMA_IOC;
	config->out_desc->addr = config->rx_ram;
	GR_USB_DOEPDMA(config->endpoint) = (uint32_t)config->out_desc;
	config->in_desc[0].flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_BSY |
				 DIEPDMA_IOC;
	config->in_desc[1].flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_BSY |
				 DIEPDMA_IOC;
	/*
	 * No need to set config->in_desc[0].addr here, because it will be set
	 * in tx_stream_handler() with the queue head pointer at that time.
	 * Meanwhile, config->in_desc[1].addr is set here once, and it won't be
	 * changed at all.
	 */
	config->in_desc[1].addr = (void *)config->consumer.queue->buffer;
	GR_USB_DIEPDMA(config->endpoint) = (uint32_t)config->in_desc;
	GR_USB_DOEPCTL(config->endpoint) = DXEPCTL_MPS(64) | DXEPCTL_USBACTEP |
					   DXEPCTL_EPTYPE_BULK |
					   DXEPCTL_CNAK | DXEPCTL_EPENA;
	GR_USB_DIEPCTL(config->endpoint) = DXEPCTL_MPS(64) | DXEPCTL_USBACTEP |
					   DXEPCTL_EPTYPE_BULK |
					   DXEPCTL_TXFNUM(config->endpoint);
	GR_USB_DAINTMSK |= DAINT_INEP(config->endpoint) |
			   DAINT_OUTEP(config->endpoint);

	*config->is_reset = 1;

	/* Flush any queued data */
	tx_stream_handler(config);
	hook_call_deferred(config->deferred_rx, 0);
}

static void usb_read(struct producer const *producer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(producer, struct usb_stream_config, producer);

	hook_call_deferred(config->deferred_rx, 0);
}

/*
 * NOTE: usb_written() should be called by IRQ handlers, so that
 *       it can be non-preemptive.
 */
static void usb_written(struct consumer const *consumer, size_t count)
{
	struct usb_stream_config const *config =
		DOWNCAST(consumer, struct usb_stream_config, consumer);

	/* USB TX transfer is active. No need to activate it. */
	if (*config->tx_in_progress) {
		struct queue const *tx_q;

		if (!*config->kicker_running)
			return;

		/*
		 * If kicker is running for too long and we already have a
		 * certain amount of data accumulated in the buffer, let's
		 * proceed even before the kicker had a chance to kick in.
		 */
		tx_q = config->consumer.queue;
		if (queue_count(tx_q) < tx_q->buffer_units_mask)
			return;

		hook_call_deferred(config->tx_kicker, -1);
		*config->kicker_running = 0;
	}

	/*
	 * if USB Endpoint has not been initialized nor in ready status,
	 * then return.
	 */
	if (!tx_fifo_is_ready(config))
		return;

	*config->tx_in_progress = 1;
	tx_stream_handler(config);
}

struct producer_ops const usb_stream_producer_ops = {
	.read = usb_read,
};

struct consumer_ops const usb_stream_consumer_ops = {
	.written = usb_written,
};
