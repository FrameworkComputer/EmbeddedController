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

struct usb_isochronous_config;

/*
 * Currently, we only support TX direction for USB isochronous transfer.
 */

/*
 * Copy `n` bytes from `src` to USB buffer.
 *
 * We are using double buffering, therefore, we need to write to the buffer that
 * hardware is not currently using.  This function will handle this for you.
 *
 * Sample usage:
 *
 *	int buffer_id = -1;  // initialize to unknown
 *	int ret;
 *	size_t dst_offset = 0, src_offset = 0;
 *	const uint8_t* buf;
 *	size_t buf_size;
 *
 *	while (1) {
 *		buf = ...;
 *		buf_size = ...;
 *		if (no more data) {
 *			buf = NULL;
 *			break;
 *		} else {
 *			ret = usb_isochronous_write_buffer(
 *				config, buf, buf_size, dst_offset,
 *				&buffer_id,
 *				0);
 *			if (ret < 0)
 *				goto FAILED;
 *			dst_offset += ret;
 *			if (ret != buf_size) {
 *				// no more space in TX buffer
 *				src_offset = ret;
 *				break;
 *			}
 *		}
 *	}
 *	// commit
 *	ret = usb_isochronous_write_buffer(
 *		config, NULL, 0, dst_offset,
 *		&buffer_id, 1);
 *	if (ret < 0)
 *		goto FAILED;
 *	if (buf)
 *		// buf[src_offset ... buf_size] haven't been sent yet, send them
 *		// later.
 *
 * On the first invocation, on success, `ret` will be number of bytes that have
 * been written, and `buffer_id` will be 0 or 1, depending on which buffer we
 * are writing.  And commit=0 means there are pending data, so buffer count
 * won't be set yet.
 *
 * On the second invocation, since buffer_id is not -1, we will return an error
 * if hardware has switched to this buffer (it means we spent too much time
 * filling buffer).  And commit=1 means we are done, and buffer count will be
 * set to `dst_offset + num_bytes_written` on success.
 *
 * @return  -EC_ERROR_CODE on failure, or number of bytes written on success.
 */
int usb_isochronous_write_buffer(
		struct usb_isochronous_config const *config,
		const uint8_t *src,
		size_t n,
		size_t dst_offset,
		int *buffer_id,
		int commit);

struct usb_isochronous_config {
	int endpoint;

	/*
	 * On TX complete, this function will be called in **interrupt
	 * context**.
	 *
	 * @param config	the usb_isochronous_config of the USB interface.
	 */
	void (*tx_callback)(struct usb_isochronous_config const *config);

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
				    SET_INTERFACE,			\
				    NUM_EXTRA_ENDPOINTS)		\
	BUILD_ASSERT(TX_SIZE > 0);					\
	BUILD_ASSERT((TX_SIZE <   64 && (TX_SIZE & 0x01) == 0) ||	\
		     (TX_SIZE < 1024 && (TX_SIZE & 0x1f) == 0));	\
	/* Declare buffer */						\
	static usb_uint CONCAT2(NAME, _ep_tx_buffer_0)[TX_SIZE / 2] __usb_ram; \
	static usb_uint CONCAT2(NAME, _ep_tx_buffer_1)[TX_SIZE / 2] __usb_ram; \
	struct usb_isochronous_config const NAME = {			\
		.endpoint  = ENDPOINT,					\
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
		.bNumEndpoints      = 1 + NUM_EXTRA_ENDPOINTS,		\
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

void usb_isochronous_tx(struct usb_isochronous_config const *config);
void usb_isochronous_event(struct usb_isochronous_config const *config,
			   enum usb_ep_event event);
int usb_isochronous_iface_handler(struct usb_isochronous_config const *config,
				  usb_uint *ep0_buf_rx,
				  usb_uint *ep0_buf_tx);
#endif /* __CROS_EC_USB_ISOCHRONOUS_H */
