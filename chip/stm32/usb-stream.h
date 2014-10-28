/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CHIP_STM32_USB_STREAM_H
#define CHIP_STM32_USB_STREAM_H

/* STM32 USB STREAM driver for Chrome EC */

#include "compile_time_macros.h"
#include "in_stream.h"
#include "out_stream.h"
#include "queue.h"
#include "usb.h"

#include <stdint.h>

/*
 * Per-USB stream state stored in RAM.  Zero initialization of this structure
 * by the BSS initialization leaves it in a valid and correctly initialized
 * state, so there is no need currently for a usb_stream_init style function.
 *
 * If this structure is changed to require non-zero initialization such a
 * function should be added.
 */
struct usb_stream_state {
	struct queue_state rx;
	struct queue_state tx;

	/*
	 * Flag indicating that there is a full RX buffer in the USB packet RAM
	 * that we were not able to move into the RX queue because there was
	 * not enough room when the packet was initially received.  The
	 * in_stream read operation checks this flag so that once there is
	 * room in the queue it can copy the RX buffer into the queue and
	 * restart USB reception by marking the RX buffer as VALID.
	 */
	int rx_waiting;
};

/*
 * Compile time Per-USB stream configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB stream.  This structure binds
 * together all information required to operate a USB stream.
 */
struct usb_stream_config {
	/*
	 * Pointer to usb_stream_state structure.  The state structure
	 * maintains per USB stream information (head and tail pointers for
	 * the queues for instance).
	 */
	struct usb_stream_state volatile *state;

	/*
	 * Endpoint index, and pointers to the USB packet RAM buffers.
	 */
	int endpoint;

	usb_uint *rx_ram;
	usb_uint *tx_ram;

	/*
	 * RX and TX queue config.  The state for the queue is stored
	 * separately in the usb_stream_state structure.
	 */
	struct queue rx;
	struct queue tx;

	/*
	 * In and Out streams, these contain pointers to the virtual function
	 * tables that implement in and out streams.  They can be used by any
	 * code that wants to read or write to a stream interface.
	 */
	struct in_stream  in;
	struct out_stream out;
};

/*
 * These function tables are defined by the USB stream driver and are used to
 * initialize the in and out streams in the usb_stream_config.
 */
extern struct in_stream_ops const usb_stream_in_stream_ops;
extern struct out_stream_ops const usb_stream_out_stream_ops;

/*
 * Convenience macro for defining USB streams and their associated state and
 * buffers.
 *
 * NAME is used to construct the names of the queue buffers, trampoline
 * functions, usb_stream_state struct, and usb_stream_config struct, the
 * latter is just called NAME.
 *
 * INTERFACE is the index of the USB interface to associate with this
 * stream.
 *
 * ENDPOINT is the index of the USB bulk endpoint used for receiving and
 * transmitting bytes.
 *
 * RX_SIZE and TX_SIZE are the size in bytes of the RX and TX queue buffers
 * respectively.
 *
 * RX_READY and TX_READY are the callback functions for the in and out streams.
 * These functions are called when there are bytes to read or space for bytes
 * to write respectively.
 */
#define USB_STREAM_CONFIG(NAME,						\
			  INTERFACE,					\
			  ENDPOINT,					\
			  RX_SIZE,					\
			  TX_SIZE,					\
			  RX_READY,					\
			  TX_READY)					\
	BUILD_ASSERT(RX_SIZE >= USB_MAX_PACKET_SIZE);			\
	BUILD_ASSERT(TX_SIZE >= USB_MAX_PACKET_SIZE);			\
	static uint8_t  CONCAT2(NAME, _rx_buffer)[RX_SIZE];		\
	static uint8_t  CONCAT2(NAME, _tx_buffer)[TX_SIZE];		\
	static usb_uint CONCAT2(NAME, _ep_rx_buffer)[USB_MAX_PACKET_SIZE / 2] __usb_ram; \
	static usb_uint CONCAT2(NAME, _ep_tx_buffer)[USB_MAX_PACKET_SIZE / 2] __usb_ram; \
	static struct usb_stream_state CONCAT2(NAME, _state);		\
	struct usb_stream_config const NAME = {				\
		.state    = &CONCAT2(NAME, _state),			\
		.endpoint = ENDPOINT,					\
		.rx_ram   = CONCAT2(NAME, _ep_rx_buffer),		\
		.tx_ram   = CONCAT2(NAME, _ep_tx_buffer),		\
		.rx = {							\
			.state        = &CONCAT2(NAME, _state.rx),	\
			.buffer_units = RX_SIZE,			\
			.unit_bytes   = 1,				\
			.buffer       = CONCAT2(NAME, _rx_buffer),	\
		},							\
		.tx = {							\
			.state        = &CONCAT2(NAME, _state.tx),	\
			.buffer_units = TX_SIZE,			\
			.unit_bytes   = 1,				\
			.buffer       = CONCAT2(NAME, _tx_buffer),	\
		},							\
		.in  = {						\
			.ready = RX_READY,				\
			.ops   = &usb_stream_in_stream_ops,		\
		},							\
		.out = {						\
			.ready = TX_READY,				\
			.ops   = &usb_stream_out_stream_ops,		\
		},							\
	};								\
	const struct usb_interface_descriptor				\
	USB_IFACE_DESC(INTERFACE) = {					\
		.bLength            = USB_DT_INTERFACE_SIZE,		\
		.bDescriptorType    = USB_DT_INTERFACE,			\
		.bInterfaceNumber   = INTERFACE,			\
		.bAlternateSetting  = 0,				\
		.bNumEndpoints      = 2,				\
		.bInterfaceClass    = USB_CLASS_VENDOR_SPEC,		\
		.bInterfaceSubClass = USB_SUBCLASS_GOOGLE_SERIAL,	\
		.bInterfaceProtocol = USB_PROTOCOL_GOOGLE_SERIAL,	\
		.iInterface         = 0,				\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 0) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = 0x80 | ENDPOINT,			\
		.bmAttributes     = 0x02 /* Bulk IN */,			\
		.wMaxPacketSize   = USB_MAX_PACKET_SIZE,		\
		.bInterval        = 10,					\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 1) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = ENDPOINT,				\
		.bmAttributes     = 0x02 /* Bulk OUT */,		\
		.wMaxPacketSize   = USB_MAX_PACKET_SIZE,		\
		.bInterval        = 0,					\
	};								\
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

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB stream driver.
 */
void usb_stream_tx(struct usb_stream_config const *config);
void usb_stream_rx(struct usb_stream_config const *config);
void usb_stream_reset(struct usb_stream_config const *config);

#endif /* CHIP_STM32_USB_STREAM_H */
