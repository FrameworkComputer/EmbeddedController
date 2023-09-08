/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_DWC_STREAM_H
#define __CROS_EC_USB_DWC_STREAM_H

/* USB STREAM driver for Chrome EC */

#include "compile_time_macros.h"
#include "consumer.h"
#include "hooks.h"
#include "producer.h"
#include "queue.h"
#include "registers.h"
#include "usb_descriptor.h"
#include "usb_hw.h"

/*
 * Compile time Per-USB stream configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB stream.  This structure binds
 * together all information required to operate a USB stream.
 */
struct usb_stream_config {
	/*
	 * Endpoint index, and pointers to the USB packet RAM buffers.
	 */
	int endpoint;
	struct dwc_usb_ep *ep;

	int *is_reset;
	int *overflow;

	/*
	 * Deferred function to call to handle USB and Queue request.
	 */
	const struct deferred_data *deferred_tx;
	const struct deferred_data *deferred_rx;

	int tx_size;
	int rx_size;

	uint8_t *tx_ram;
	uint8_t *rx_ram;

	struct consumer consumer;
	struct producer producer;
};

/*
 * These function tables are defined by the USB Stream driver and are used to
 * initialize the consumer and producer in the usb_stream_config.
 */
extern struct consumer_ops const usb_stream_consumer_ops;
extern struct producer_ops const usb_stream_producer_ops;

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
 *
 * RX_IDX and TX_IDX defined the order in which the OUT(RX) and IN(TX) endpoints
 * are listed in the interface descriptor.  I most circumstances, the order
 * makes no difference, but the CMSIS-DAP protocol requires that the OUT
 * endpoint is the first, and IN is the second.
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
#define USB_STREAM_CONFIG_FULL(NAME, INTERFACE, INTERFACE_CLASS,               \
			       INTERFACE_SUBCLASS, INTERFACE_PROTOCOL,         \
			       INTERFACE_NAME, ENDPOINT, RX_SIZE, TX_SIZE,     \
			       RX_QUEUE, TX_QUEUE, RX_IDX, TX_IDX)             \
                                                                               \
	static uint8_t CONCAT2(NAME, _buf_rx_)[RX_SIZE];                       \
	static uint8_t CONCAT2(NAME, _buf_tx_)[TX_SIZE];                       \
	static int CONCAT2(NAME, _is_reset_);                                  \
	static int CONCAT2(NAME, _overflow_);                                  \
	static void CONCAT2(NAME, _deferred_tx_)(void);                        \
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_tx_));                        \
	static void CONCAT2(NAME, _deferred_rx_)(void);                        \
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_rx_));                        \
	struct usb_stream_config const NAME = {				\
		.endpoint     = ENDPOINT,				\
		.is_reset     = &CONCAT2(NAME, _is_reset_),		\
		.overflow     = &CONCAT2(NAME, _overflow_),		\
		.deferred_tx  = &CONCAT2(NAME, _deferred_tx__data),	\
		.deferred_rx  = &CONCAT2(NAME, _deferred_rx__data),	\
		.tx_size      = TX_SIZE,				\
		.rx_size      = RX_SIZE,				\
		.tx_ram       = CONCAT2(NAME, _buf_tx_),		\
		.rx_ram       = CONCAT2(NAME, _buf_rx_),		\
		.consumer  = {						\
			.queue = &TX_QUEUE,				\
			.ops   = &usb_stream_consumer_ops,		\
		},							\
		.producer  = {						\
			.queue = &RX_QUEUE,				\
			.ops   = &usb_stream_producer_ops,		\
		},							\
	};                           \
	const struct usb_interface_descriptor USB_IFACE_DESC(INTERFACE) = {    \
		.bLength = USB_DT_INTERFACE_SIZE,                              \
		.bDescriptorType = USB_DT_INTERFACE,                           \
		.bInterfaceNumber = INTERFACE,                                 \
		.bAlternateSetting = 0,                                        \
		.bNumEndpoints = 2,                                            \
		.bInterfaceClass = INTERFACE_CLASS,                            \
		.bInterfaceSubClass = INTERFACE_SUBCLASS,                      \
		.bInterfaceProtocol = INTERFACE_PROTOCOL,                      \
		.iInterface = INTERFACE_NAME,                                  \
	};                                                                     \
	const struct usb_endpoint_descriptor USB_EP_DESC(INTERFACE,            \
							 TX_IDX) = {           \
		.bLength = USB_DT_ENDPOINT_SIZE,                               \
		.bDescriptorType = USB_DT_ENDPOINT,                            \
		.bEndpointAddress = 0x80 | ENDPOINT,                           \
		.bmAttributes = 0x02 /* Bulk IN */,                            \
		.wMaxPacketSize = TX_SIZE,                                     \
		.bInterval = 10,                                               \
	};                                                                     \
	const struct usb_endpoint_descriptor USB_EP_DESC(INTERFACE,            \
							 RX_IDX) = {           \
		.bLength = USB_DT_ENDPOINT_SIZE,                               \
		.bDescriptorType = USB_DT_ENDPOINT,                            \
		.bEndpointAddress = ENDPOINT,                                  \
		.bmAttributes = 0x02 /* Bulk OUT */,                           \
		.wMaxPacketSize = RX_SIZE,                                     \
		.bInterval = 0,                                                \
	};                                                                     \
	static void CONCAT2(NAME, _deferred_tx_)(void)                         \
	{                                                                      \
		tx_stream_handler(&NAME);                                      \
	}                                                                      \
	static void CONCAT2(NAME, _deferred_rx_)(void)                         \
	{                                                                      \
		rx_stream_handler(&NAME);                                      \
	}                                                                      \
	static void CONCAT2(NAME, _ep_tx)(void)                                \
	{                                                                      \
		usb_epN_tx(ENDPOINT);                                          \
	}                                                                      \
	static void CONCAT2(NAME, _ep_rx)(void)                                \
	{                                                                      \
		usb_epN_rx(ENDPOINT);                                          \
	}                                                                      \
	static void CONCAT2(NAME, _ep_event)(enum usb_ep_event evt)            \
	{                                                                      \
		usb_stream_event(&NAME, evt);                                  \
	}                                                                      \
	struct dwc_usb_ep CONCAT2(NAME, _ep_ctl) = {                           \
		.max_packet = USB_MAX_PACKET_SIZE,                             \
		.tx_fifo = ENDPOINT,                                           \
		.out_pending = 0,                                              \
		.out_expected = 0,                                             \
		.out_data = 0,                                                 \
		.out_databuffer = CONCAT2(NAME, _buf_rx_),                     \
		.out_databuffer_max = RX_SIZE,                                 \
		.rx_deferred = &CONCAT2(NAME, _deferred_rx__data),             \
		.in_packets = 0,                                               \
		.in_pending = 0,                                               \
		.in_data = 0,                                                  \
		.in_databuffer = CONCAT2(NAME, _buf_tx_),                      \
		.in_databuffer_max = TX_SIZE,                                  \
		.tx_deferred = &CONCAT2(NAME, _deferred_tx__data),             \
	};                                                                     \
	USB_DECLARE_EP(ENDPOINT, CONCAT2(NAME, _ep_tx), CONCAT2(NAME, _ep_rx), \
		       CONCAT2(NAME, _ep_event));

/* This is a short version for declaring Google serial endpoints */
#define USB_STREAM_CONFIG(NAME, INTERFACE, INTERFACE_NAME, ENDPOINT, RX_SIZE, \
			  TX_SIZE, RX_QUEUE, TX_QUEUE)                        \
	USB_STREAM_CONFIG_FULL(NAME, INTERFACE, USB_CLASS_VENDOR_SPEC,        \
			       USB_SUBCLASS_GOOGLE_SERIAL,                    \
			       USB_PROTOCOL_GOOGLE_SERIAL, INTERFACE_NAME,    \
			       ENDPOINT, RX_SIZE, TX_SIZE, RX_QUEUE, TX_QUEUE)

/*
 * Handle USB and Queue request in a deferred callback.
 */
int rx_stream_handler(struct usb_stream_config const *config);
int tx_stream_handler(struct usb_stream_config const *config);

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB stream driver.
 */
void usb_stream_tx(struct usb_stream_config const *config);
void usb_stream_rx(struct usb_stream_config const *config);
void usb_stream_event(struct usb_stream_config const *config,
		      enum usb_ep_event evt);

#endif /* __CROS_EC_USB_STREAM_H */
