/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CHIP_STM32_USB_GPIO_H
#define CHIP_STM32_USB_GPIO_H

/* STM32 USB GPIO driver for Chrome EC */

#include "compile_time_macros.h"
#include "usb.h"

/*
 * Compile time Per-USB gpio configuration stored in flash.  Instances of this
 * structure are provided by the user of the USB gpio.  This structure binds
 * together all information required to operate a USB gpio.
 */
struct usb_gpio_config {
	/*
	 * Endpoint index, and pointers to the USB packet RAM buffers.
	 */
	int endpoint;

	usb_uint *rx_ram;
	usb_uint *tx_ram;

	/*
	 * GPIO list
	 */
	enum gpio_signal const *gpios;
	size_t num_gpios;
};

#define USB_GPIO_RX_PACKET_SIZE 8
#define USB_GPIO_TX_PACKET_SIZE 4

/*
 * Convenience macro for defining a USB GPIO driver and its associated state.
 *
 * NAME is used to construct the names of the trampoline functions,
 * usb_gpio_state struct, and usb_gpio_config struct, the latter is just
 * called NAME.
 *
 * INTERFACE is the index of the USB interface to associate with this
 * GPIO driver.
 *
 * ENDPOINT is the index of the USB bulk endpoint used for receiving and
 * transmitting bytes.
 */
#define USB_GPIO_CONFIG(NAME,						\
			GPIO_LIST,					\
			INTERFACE,					\
			ENDPOINT)					\
	BUILD_ASSERT(ARRAY_SIZE(GPIO_LIST) <= 32);			\
	static usb_uint CONCAT2(NAME, _ep_rx_buffer)[USB_GPIO_RX_PACKET_SIZE / 2] __usb_ram;	\
	static usb_uint CONCAT2(NAME, _ep_tx_buffer)[USB_GPIO_TX_PACKET_SIZE / 2] __usb_ram;	\
	struct usb_gpio_config const NAME = {				\
		.endpoint  = ENDPOINT,					\
		.rx_ram    = CONCAT2(NAME, _ep_rx_buffer),		\
		.tx_ram    = CONCAT2(NAME, _ep_tx_buffer),		\
		.gpios     = GPIO_LIST,					\
		.num_gpios = ARRAY_SIZE(GPIO_LIST),			\
	};								\
	const struct usb_interface_descriptor				\
	USB_IFACE_DESC(INTERFACE) = {					\
		.bLength            = USB_DT_INTERFACE_SIZE,		\
		.bDescriptorType    = USB_DT_INTERFACE,			\
		.bInterfaceNumber   = INTERFACE,			\
		.bAlternateSetting  = 0,				\
		.bNumEndpoints      = 2,				\
		.bInterfaceClass    = USB_CLASS_VENDOR_SPEC,		\
		.bInterfaceSubClass = 0,				\
		.bInterfaceProtocol = 0,				\
		.iInterface         = 0,				\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 0) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = 0x80 | ENDPOINT,			\
		.bmAttributes     = 0x02 /* Bulk IN */,			\
		.wMaxPacketSize   = USB_GPIO_TX_PACKET_SIZE,		\
		.bInterval        = 10,					\
	};								\
	const struct usb_endpoint_descriptor				\
	USB_EP_DESC(INTERFACE, 1) = {					\
		.bLength          = USB_DT_ENDPOINT_SIZE,		\
		.bDescriptorType  = USB_DT_ENDPOINT,			\
		.bEndpointAddress = ENDPOINT,				\
		.bmAttributes     = 0x02 /* Bulk OUT */,		\
		.wMaxPacketSize   = USB_GPIO_RX_PACKET_SIZE,		\
		.bInterval        = 0,					\
	};								\
	static void CONCAT2(NAME, _ep_tx)(void)				\
	{								\
		usb_gpio_tx(&NAME);					\
	}								\
	static void CONCAT2(NAME, _ep_rx)(void)				\
	{								\
		usb_gpio_rx(&NAME);					\
	}								\
	static void CONCAT2(NAME, _ep_reset)(void)			\
	{								\
		usb_gpio_reset(&NAME);					\
	}								\
	USB_DECLARE_EP(ENDPOINT,					\
		       CONCAT2(NAME, _ep_tx),				\
		       CONCAT2(NAME, _ep_rx),				\
		       CONCAT2(NAME, _ep_reset));

/*
 * These functions are used by the trampoline functions defined above to
 * connect USB endpoint events with the generic USB GPIO driver.
 */
void usb_gpio_tx(struct usb_gpio_config const *config);
void usb_gpio_rx(struct usb_gpio_config const *config);
void usb_gpio_reset(struct usb_gpio_config const *config);

#endif /* CHIP_STM32_USB_GPIO_H */
