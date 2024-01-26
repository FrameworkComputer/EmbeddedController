/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_STREAM_H
#define __CROS_EC_USB_STREAM_H

#if defined(CHIP_FAMILY_STM32F4)
#include "usb_dwc_stream.h"
#else

/* STM32 USB STREAM driver for Chrome EC */

#include "compile_time_macros.h"
#include "consumer.h"
#include "hooks.h"
#include "producer.h"
#include "queue.h"
#include "usart.h"
#include "usb_descriptor.h"
#include "usb_hw.h"

#include <stdint.h>

/*
 * Per-USB stream state stored in RAM.  Zero initialization of this structure
 * by the BSS initialization leaves it in a valid and correctly initialized
 * state, so there is no need currently for a usb_stream_init style function.
 */
struct usb_stream_state {
	/*
	 * Flag indicating that there is a full RX buffer in the USB packet RAM
	 * that we were not able to move into the RX queue because there was
	 * not enough room when the packet was initially received.  The
	 * producer read operation checks this flag so that once there is
	 * room in the queue it can copy the RX buffer into the queue and
	 * restart USB reception by marking the RX buffer as VALID.
	 */
	int rx_waiting;
	/*
	 * Flag indicating that the incoming data on the USB link are discarded.
	 */
	int rx_disabled;
};

/*
 * Compile time Per-USB stream configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB stream.  This structure binds
 * together all information required to operate a USB stream.
 */
struct usb_stream_config {
	/*
	 * Pointer to usb_stream_state structure.  The state structure
	 * maintains per USB stream information.
	 */
	struct usb_stream_state volatile *state;

	/*
	 * Endpoint index, and pointers to the USB packet RAM buffers.
	 */
	int endpoint;

	/*
	 * Deferred function to call to handle USB and Queue request.
	 */
	const struct deferred_data *deferred;

	size_t rx_size;
	size_t tx_size;

	usb_uint *rx_ram;
	usb_uint *tx_ram;

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
	BUILD_ASSERT(RX_SIZE <= USB_MAX_PACKET_SIZE);                          \
	BUILD_ASSERT(TX_SIZE <= USB_MAX_PACKET_SIZE);                          \
	BUILD_ASSERT(RX_SIZE > 0);                                             \
	BUILD_ASSERT(TX_SIZE > 0);                                             \
	BUILD_ASSERT((RX_SIZE < 64 && (RX_SIZE & 0x01) == 0) ||                \
		     (RX_SIZE < 1024 && (RX_SIZE & 0x1f) == 0));               \
	BUILD_ASSERT((TX_SIZE < 64 && (TX_SIZE & 0x01) == 0) ||                \
		     (TX_SIZE < 1024 && (TX_SIZE & 0x1f) == 0));               \
                                                                               \
	static usb_uint CONCAT2(NAME, _ep_rx_buffer)[RX_SIZE / 2] __usb_ram;   \
	static usb_uint CONCAT2(NAME, _ep_tx_buffer)[TX_SIZE / 2] __usb_ram;   \
	static struct usb_stream_state CONCAT2(NAME, _state);                  \
	static void CONCAT2(NAME, _deferred_)(void);                           \
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_));                           \
	struct usb_stream_config const NAME = {				\
		.state     = &CONCAT2(NAME, _state),			\
		.endpoint  = ENDPOINT,					\
		.deferred  = &CONCAT2(NAME, _deferred__data),		\
		.rx_size   = RX_SIZE,					\
		.tx_size   = TX_SIZE,					\
		.rx_ram    = CONCAT2(NAME, _ep_rx_buffer),		\
		.tx_ram    = CONCAT2(NAME, _ep_tx_buffer),		\
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
	static void CONCAT2(NAME, _ep_tx)(void)                                \
	{                                                                      \
		usb_stream_tx(&NAME);                                          \
	}                                                                      \
	static void CONCAT2(NAME, _ep_rx)(void)                                \
	{                                                                      \
		usb_stream_rx(&NAME);                                          \
	}                                                                      \
	static void CONCAT2(NAME, _ep_event)(enum usb_ep_event evt)            \
	{                                                                      \
		usb_stream_event(&NAME, evt);                                  \
	}                                                                      \
	USB_DECLARE_EP(ENDPOINT, CONCAT2(NAME, _ep_tx), CONCAT2(NAME, _ep_rx), \
		       CONCAT2(NAME, _ep_event));                              \
	static void CONCAT2(NAME, _deferred_)(void)                            \
	{                                                                      \
		usb_stream_deferred(&NAME);                                    \
	}

/* This is a short version for declaring Google serial endpoints */
#define USB_STREAM_CONFIG(NAME, INTERFACE, INTERFACE_NAME, ENDPOINT, RX_SIZE,  \
			  TX_SIZE, RX_QUEUE, TX_QUEUE)                         \
	USB_STREAM_CONFIG_FULL(NAME, INTERFACE, USB_CLASS_VENDOR_SPEC,         \
			       USB_SUBCLASS_GOOGLE_SERIAL,                     \
			       USB_PROTOCOL_GOOGLE_SERIAL, INTERFACE_NAME,     \
			       ENDPOINT, RX_SIZE, TX_SIZE, RX_QUEUE, TX_QUEUE, \
			       1, 0)

/* Declare a utility interface for setting parity/baud. */
#define USB_USART_IFACE(NAME, INTERFACE, USART_CFG)                      \
	static int CONCAT2(NAME, _interface_)(usb_uint * rx_buf,         \
					      usb_uint * tx_buf)         \
	{                                                                \
		return usb_usart_interface(&NAME, &USART_CFG, INTERFACE, \
					   rx_buf, tx_buf);              \
	}                                                                \
	USB_DECLARE_IFACE(INTERFACE, CONCAT2(NAME, _interface_))

/* This is a medium version for declaring Google serial endpoints */
#define USB_STREAM_CONFIG_USART_IFACE(NAME, INTERFACE, INTERFACE_NAME,         \
				      ENDPOINT, RX_SIZE, TX_SIZE, RX_QUEUE,    \
				      TX_QUEUE, USART_CFG)                     \
	USB_STREAM_CONFIG_FULL(NAME, INTERFACE, USB_CLASS_VENDOR_SPEC,         \
			       USB_SUBCLASS_GOOGLE_SERIAL,                     \
			       USB_PROTOCOL_GOOGLE_SERIAL, INTERFACE_NAME,     \
			       ENDPOINT, RX_SIZE, TX_SIZE, RX_QUEUE, TX_QUEUE, \
			       1, 0);                                          \
	USB_USART_IFACE(NAME, INTERFACE, USART_CFG)

/*
 * Handle USB and Queue request in a deferred callback.
 */
void usb_stream_deferred(struct usb_stream_config const *config);

/*
 * Handle control interface requests.
 */
enum usb_usart {
	USB_USART_REQ_PARITY = 0,
	USB_USART_SET_PARITY = 1,
	USB_USART_REQ_BAUD = 2,
	USB_USART_SET_BAUD = 3,
	USB_USART_BREAK = 4,
};

/*
 * baud rate is req/set in multiples of 100, to avoid overflowing
 * 16-bit integer.
 */
#define USB_USART_BAUD_MULTIPLIER 100

int usb_usart_interface(struct usb_stream_config const *config,
			struct usart_config const *usart, int interface,
			usb_uint *rx_buf, usb_uint *tx_buf);

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB stream driver.
 */
void usb_stream_tx(struct usb_stream_config const *config);
void usb_stream_rx(struct usb_stream_config const *config);
void usb_stream_event(struct usb_stream_config const *config,
		      enum usb_ep_event evt);

#endif /* defined(CHIP_FAMILY_STM32F4) */
#endif /* __CROS_EC_USB_STREAM_H */
