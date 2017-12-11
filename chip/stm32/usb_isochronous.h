/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_ISOCHRONOUS_H
#define __CROS_EC_USB_ISOCHRONOUS_H

#include "common.h"
#include "compile_time_macros.h"
#include "hooks.h"
#include "usb_descriptor.h"
#include "usb_hw.h"

/* Currently, we only support TX direction for USB isochronous transfer. */

struct usb_isochronous_config {
	int endpoint;

	/*
	 * Deferred function to call to handle USB request.
	 */
	const struct deferred_data *deferred;

	/*
	 * On TX complete, this function will be called to ask for more data to
	 * transmit.
	 *
	 * @param  usb_addr	USB buffer, an uint8_t pointer that can be
	 *			passed to memcpy_to_usbram()
	 * @param  tx_size	config->tx_size
	 * @return size_t	Number of bytes written to USB buffer
	 */
	size_t (*tx_callback)(usb_uint *usb_addr, size_t tx_size);

	/*
	 * Received SET_INTERFACE request.
	 *
	 * @param  alternate_setting	new bAlternateSetting value.
	 * @param  interface		interface number.
	 * @return int			0 for success, -1 for unknown setting.
	 */
	int (*set_interface)(usb_uint alternate_setting, usb_uint interface);

	/* USB packet RAM buffer size. */
	size_t tx_size;
	/* USB packet RAM buffers. */
	usb_uint *tx_ram[2];
};

/* Define an USB isochronous interface */
#define USB_ISOCHRONOUS_CONFIG_FULL(NAME,				\
				    INTERFACE,				\
				    INTERFACE_CLASS,			\
				    INTERFACE_SUBCLASS,			\
				    INTERFACE_PROTOCOL,			\
				    INTERFACE_NAME,			\
				    ENDPOINT,				\
				    TX_SIZE,				\
				    TX_CALLBACK,			\
				    SET_INTERFACE)			\
	BUILD_ASSERT(TX_SIZE > 0);					\
	BUILD_ASSERT((TX_SIZE <   64 && (TX_SIZE & 0x01) == 0) ||	\
		     (TX_SIZE < 1024 && (TX_SIZE & 0x1f) == 0));	\
	/* Declare buffer */						\
	static usb_uint CONCAT2(NAME, _ep_tx_buffer_0)[TX_SIZE / 2] __usb_ram; \
	static usb_uint CONCAT2(NAME, _ep_tx_buffer_1)[TX_SIZE / 2] __usb_ram; \
	static void CONCAT2(NAME, _deferred_)(void);			\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_));			\
	struct usb_isochronous_config const NAME = {			\
		.endpoint  = ENDPOINT,					\
		.deferred  = &CONCAT2(NAME, _deferred__data),		\
		.tx_callback = TX_CALLBACK,				\
		.set_interface = SET_INTERFACE,				\
		.tx_size   = TX_SIZE,					\
		.tx_ram    = {						\
			CONCAT2(NAME, _ep_tx_buffer_0),			\
			CONCAT2(NAME, _ep_tx_buffer_1),			\
		},							\
	};								\
	const struct usb_interface_descriptor				\
	USB_IFACE_DESC(INTERFACE) = {					\
		.bLength            = USB_DT_INTERFACE_SIZE,		\
		.bDescriptorType    = USB_DT_INTERFACE,			\
		.bInterfaceNumber   = INTERFACE,			\
		.bAlternateSetting  = 0,				\
		.bNumEndpoints      = 0,				\
		.bInterfaceClass    = INTERFACE_CLASS,			\
		.bInterfaceSubClass = INTERFACE_SUBCLASS,		\
		.bInterfaceProtocol = INTERFACE_PROTOCOL,		\
		.iInterface         = INTERFACE_NAME,			\
	};								\
	const struct usb_interface_descriptor				\
	USB_CONF_DESC(CONCAT3(iface, INTERFACE, _1iface)) = {		\
		.bLength            = USB_DT_INTERFACE_SIZE,		\
		.bDescriptorType    = USB_DT_INTERFACE,			\
		.bInterfaceNumber   = INTERFACE,			\
		.bAlternateSetting  = 1,				\
		.bNumEndpoints      = 1,				\
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
		.bmAttributes     = 0x01 /* Isochronous IN */,		\
		.wMaxPacketSize   = TX_SIZE,				\
		.bInterval        = 1,					\
	};								\
	static void CONCAT2(NAME, _ep_tx)(void)				\
	{								\
		usb_isochronous_tx(&NAME);				\
	}								\
	static void CONCAT2(NAME, _ep_event)(enum usb_ep_event evt)	\
	{								\
		usb_isochronous_event(&NAME, evt);			\
	}								\
	static int CONCAT2(NAME, _handler)(usb_uint *rx, usb_uint *tx)	\
	{								\
		return usb_isochronous_iface_handler(&NAME, rx, tx);	\
	}								\
	USB_DECLARE_IFACE(INTERFACE, CONCAT2(NAME, _handler));		\
	USB_DECLARE_EP(ENDPOINT,					\
		       CONCAT2(NAME, _ep_tx),				\
		       CONCAT2(NAME, _ep_tx),				\
		       CONCAT2(NAME, _ep_event));			\
	static void CONCAT2(NAME, _deferred_)(void)			\
	{								\
		usb_isochronous_deferred(&NAME);			\
	}

void usb_isochronous_deferred(struct usb_isochronous_config const *config);
void usb_isochronous_tx(struct usb_isochronous_config const *config);
void usb_isochronous_event(struct usb_isochronous_config const *config,
			   enum usb_ep_event event);
int usb_isochronous_iface_handler(struct usb_isochronous_config const *config,
				  usb_uint *ep0_buf_rx,
				  usb_uint *ep0_buf_tx);
#endif /* __CROS_EC_USB_ISOCHRONOUS_H */
