/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_STREAM_H
#define __CROS_EC_USB_STREAM_H

/* USB STREAM driver for Chrome EC */

#include "compile_time_macros.h"
#include "consumer.h"
#include "hooks.h"
#include "registers.h"
#include "producer.h"
#include "queue.h"
#include "usb_descriptor.h"
#include "usb_hw.h"

#define MAX_IN_DESC	2

/*
 * Compile time Per-USB stream configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB stream.  This structure binds
 * together all information required to operate a USB stream.
 */
struct usb_stream_config {
	/*
	 * Endpoint index, and pointers to the USB packet RAM buffers.
	 */
	uint16_t endpoint;
	uint16_t is_uart_console;

	/* USB TX transfer is in progress */
	uint8_t *tx_in_progress;
	uint8_t *kicker_running;
	uint8_t *is_reset;

	/*
	 * Deferred function to call to handle USB and Queue request.
	 */
	const struct deferred_data *deferred_rx;
	const struct deferred_data *tx_kicker;

	int tx_size;
	int rx_size;

	uint8_t *rx_ram;

	struct consumer consumer;
	struct producer producer;

	struct g_usb_desc *out_desc;
	struct g_usb_desc *in_desc;

	int *rx_handled;
	/* Number of buffer units in TX queue in transit.
	 * This is to advance queue tail pointer when the transfer is done.
	 */
	size_t *tx_handled;
};

/*
 * These function tables are defined by the USB Stream driver and are used to
 * initialize the consumer and producer in the usb_stream_config.
 */
extern struct consumer_ops const usb_stream_consumer_ops;
extern struct producer_ops const usb_stream_producer_ops;

/* Need to define these so that other than Cr50 boards compile cleanly. */
#ifndef USB_EP_EC
#define USB_EP_EC -1
#endif
#ifndef USB_EP_AP
#define USB_EP_AP -1
#endif

/*
 * Convenience macro for defining USB streams and their associated state and
 * buffers.
 *
 * NAME is used to construct the names of the packet RAM buffers, trampoline
 * functions, usb_stream_state struct, and usb_stream_config struct, the
 * latter is just called NAME.
 *
 * INTERFACE is the index of the USB interface to associate with this
 * stream.
 *
 * INTERFACE_CLASS, INTERFACE_SUBCLASS, INTERFACE_PROTOCOL are the
 * .bInterfaceClass, .bInterfaceSubClass, and .bInterfaceProtocol fields
 * respectively in the USB interface descriptor.
 *
 * INTERFACE_NAME is the index of the USB string descriptor (iInterface).
 *
 * ENDPOINT is the index of the USB bulk endpoint used for receiving and
 * transmitting bytes.
 *
 * RX_SIZE and TX_SIZE are the number of bytes of USB packet RAM to allocate
 * for the RX and TX packets respectively.  The valid values for these
 * parameters are dictated by the USB peripheral.
 *
 * RX_QUEUE and TX_QUEUE are the names of the RX and TX queues that this driver
 * should write to and read from respectively.
 */
/*
 * The following assertions can not be made because they require access to
 * non-const fields, but should be kept in mind.
 *
 * BUILD_ASSERT(RX_QUEUE.buffer_units >= RX_SIZE);
 * BUILD_ASSERT(TX_QUEUE.buffer_units >= TX_SIZE);
 * BUILD_ASSERT(RX_QUEUE.unit_bytes == 1);
 * BUILD_ASSERT(TX_QUEUE.unit_bytes == 1);
 */
#define USB_STREAM_CONFIG_FULL(NAME,					\
			       INTERFACE,				\
			       INTERFACE_CLASS,				\
			       INTERFACE_SUBCLASS,			\
			       INTERFACE_PROTOCOL,			\
			       INTERFACE_NAME,				\
			       ENDPOINT,				\
			       RX_SIZE,					\
			       TX_SIZE,					\
			       RX_QUEUE,				\
			       TX_QUEUE)				\
									\
	static struct g_usb_desc CONCAT2(NAME, _out_desc_);		\
	static struct g_usb_desc CONCAT2(NAME, _in_desc_)[MAX_IN_DESC];	\
	static uint8_t CONCAT2(NAME, _buf_rx_)[RX_SIZE];		\
	static uint8_t CONCAT2(NAME, _tx_in_progress_);			\
	static uint8_t CONCAT2(NAME, _kicker_running_);			\
	static uint8_t CONCAT2(NAME, _is_reset_);			\
	static void CONCAT2(NAME, _deferred_rx_)(void);			\
	static void CONCAT2(NAME, _tx_kicker_)(void);			\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_rx_));			\
	DECLARE_DEFERRED(CONCAT2(NAME, _tx_kicker_));			\
	static int CONCAT2(NAME, _rx_handled);				\
	static size_t CONCAT2(NAME, _tx_handled);			\
	struct usb_stream_config const NAME = {				\
		.endpoint     = ENDPOINT,				\
		.is_uart_console = ((ENDPOINT == USB_EP_EC) ||		\
				    (ENDPOINT == USB_EP_CONSOLE) ||	\
				    (ENDPOINT == USB_EP_AP)),		\
		.tx_in_progress = &CONCAT2(NAME, _tx_in_progress_),	\
		.kicker_running = &CONCAT2(NAME, _kicker_running_),	\
		.is_reset     = &CONCAT2(NAME, _is_reset_),		\
		.in_desc      = &CONCAT2(NAME, _in_desc_)[0],		\
		.out_desc     = &CONCAT2(NAME, _out_desc_),		\
		.deferred_rx  = &CONCAT2(NAME, _deferred_rx__data),	\
		.tx_kicker    = &CONCAT2(NAME, _tx_kicker__data),	\
		.tx_size      = TX_SIZE,				\
		.rx_size      = RX_SIZE,				\
		.rx_ram       = CONCAT2(NAME, _buf_rx_),		\
		.consumer  = {						\
			.queue = &TX_QUEUE,				\
			.ops   = &usb_stream_consumer_ops,		\
		},							\
		.producer  = {						\
			.queue = &RX_QUEUE,				\
			.ops   = &usb_stream_producer_ops,		\
		},							\
		.rx_handled   = &CONCAT2(NAME, _rx_handled),		\
		.tx_handled   = &CONCAT2(NAME, _tx_handled),		\
	};								\
	const struct usb_interface_descriptor				\
	USB_IFACE_DESC(INTERFACE) = {					\
		.bLength            = USB_DT_INTERFACE_SIZE,		\
		.bDescriptorType    = USB_DT_INTERFACE,			\
		.bInterfaceNumber   = INTERFACE,			\
		.bAlternateSetting  = 0,				\
		.bNumEndpoints      = 2,				\
		.bInterfaceClass    = INTERFACE_CLASS,			\
		.bInterfaceSubClass = INTERFACE_SUBCLASS,		\
		.bInterfaceProtocol = INTERFACE_PROTOCOL,		\
		.iInterface         = INTERFACE_NAME,			\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 0) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = 0x80 | ENDPOINT,			\
		.bmAttributes     = 0x02 /* Bulk IN */,			\
		.wMaxPacketSize   = TX_SIZE,				\
		.bInterval        = 10,					\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 1) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = ENDPOINT,				\
		.bmAttributes     = 0x02 /* Bulk OUT */,		\
		.wMaxPacketSize   = RX_SIZE,				\
		.bInterval        = 0,					\
	};								\
	static void CONCAT2(NAME, _deferred_rx_)(void)			\
	{ rx_stream_handler(&NAME); }					\
	static void CONCAT2(NAME, _tx_kicker_)(void)			\
	{ tx_stream_kicker(&NAME); }					\
	static void CONCAT2(NAME, _ep_tx)(void)				\
	{								\
		usb_stream_tx(&NAME);					\
	}								\
	static void CONCAT2(NAME, _ep_rx)(void)				\
	{								\
		usb_stream_rx(&NAME);					\
	}								\
	static void CONCAT2(NAME, _ep_reset)(void)			\
	{								\
		usb_stream_reset(&NAME);				\
	}								\
	USB_DECLARE_EP(ENDPOINT,					\
		       CONCAT2(NAME, _ep_tx),				\
		       CONCAT2(NAME, _ep_rx),				\
		       CONCAT2(NAME, _ep_reset));

/* This is a short version for declaring Google serial endpoints */
#define USB_STREAM_CONFIG(NAME,						\
			  INTERFACE,					\
			  INTERFACE_NAME,				\
			  ENDPOINT,					\
			  RX_SIZE,					\
			  TX_SIZE,					\
			  RX_QUEUE,					\
			  TX_QUEUE)					\
	USB_STREAM_CONFIG_FULL(NAME,					\
			       INTERFACE,				\
			       USB_CLASS_VENDOR_SPEC,			\
			       USB_SUBCLASS_GOOGLE_SERIAL,		\
			       USB_PROTOCOL_GOOGLE_SERIAL,		\
			       INTERFACE_NAME,				\
			       ENDPOINT,				\
			       RX_SIZE,					\
			       TX_SIZE,					\
			       RX_QUEUE,				\
			       TX_QUEUE)

/*
 * Handle USB and Queue request in a deferred callback.
 */
void rx_stream_handler(struct usb_stream_config const *config);
void tx_stream_kicker(struct usb_stream_config const *config);

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB stream driver.
 */
void usb_stream_tx(struct usb_stream_config const *config);
void usb_stream_rx(struct usb_stream_config const *config);
void usb_stream_reset(struct usb_stream_config const *config);

/*
 * Return non-zero if the USB stream is reset, or 0 otherwise
 */
int tx_fifo_is_ready(struct usb_stream_config const *config);
#endif /* __CROS_EC_USB_STREAM_H */
