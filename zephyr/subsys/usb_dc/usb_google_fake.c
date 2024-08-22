/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_dc.h"

#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>

#include <usb_descriptor.h>

#define USB_SUBCLASS_GOOGLE_FAKE 0xFF
#define USB_PROTOCOL_GOOGLE_FAKE 0xFF
#define AUTO_EP_IN 0x80

#if !defined(CONFIG_USB_DC_HID_KEYBOARD) && !defined(CONFIG_USB_DC_HID_TOUCHPAD)
/* Keyboard and touchpad devices are both disabled in RW firmware */
#define USB_GFAKE_DEVICE_COUNT 0
#elif defined(CONFIG_USB_DC_HID_KEYBOARD) && defined(CONFIG_USB_DC_HID_TOUCHPAD)
/* Keyboard and touchpad devices are both enabled in RW firmware */
#define USB_GFAKE_DEVICE_COUNT 2
#else
/* Keyboard or touchpad devices are both disabled in RW firmware */
#define USB_GFAKE_DEVICE_COUNT 1
#endif

enum google_fake_ep_index {
	IN_EP_IDX,
	EP_NUM,
};

struct usb_google_fake_config {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_in_ep;
} __packed;

#define INITIALIZER_IF(num_ep, iface_class, iface_subclass, iface_proto)      \
	{                                                                     \
		.bLength = sizeof(struct usb_if_descriptor),                  \
		.bDescriptorType = USB_DESC_INTERFACE, .bInterfaceNumber = 0, \
		.bAlternateSetting = 0, .bNumEndpoints = num_ep,              \
		.bInterfaceClass = iface_class,                               \
		.bInterfaceSubClass = iface_subclass,                         \
		.bInterfaceProtocol = iface_proto, .iInterface = 0,           \
	}

#define INITIALIZER_IF_EP(addr, attr, mps)                              \
	{                                                               \
		.bLength = sizeof(struct usb_ep_descriptor),            \
		.bDescriptorType = USB_DESC_ENDPOINT,                   \
		.bEndpointAddress = addr, .bmAttributes = attr,         \
		.wMaxPacketSize = sys_cpu_to_le16(mps), .bInterval = 0, \
	}

#define DEFINE_GFAKE_DESCR(x, _)                                           \
	USBD_CLASS_DESCR_DEFINE(primary, gfake##x)                         \
	struct usb_google_fake_config google_fake_cfg_##x = {              \
		/* Interface descriptor */                                 \
		.if0 = INITIALIZER_IF(EP_NUM, USB_BCC_VENDOR,              \
				      USB_SUBCLASS_GOOGLE_FAKE,            \
				      USB_PROTOCOL_GOOGLE_FAKE),           \
		.if0_in_ep = INITIALIZER_IF_EP(AUTO_EP_IN, USB_DC_EP_BULK, \
					       USB_MAX_FS_BULK_MPS),       \
	}

#define INITIALIZER_EP_DATA(cb, addr)         \
	{                                     \
		.ep_cb = cb, .ep_addr = addr, \
	}

#define DEFINE_GFAKE_EP(x, _)                                              \
	static struct usb_ep_cfg_data gfake_ep_data_##x[] = {              \
		INITIALIZER_EP_DATA(usb_transfer_ep_callback, AUTO_EP_IN), \
	}

#define DEFINE_GFAKE_CFG_DATA(x, _) \
	USBD_DEFINE_CFG_DATA(google_fake_config_##x) = {			\
		.usb_device_description = NULL,				\
		.interface_config = google_fake_interface_config,		\
		.interface_descriptor = &google_fake_cfg_##x.if0,		\
		.interface = {						\
			.class_handler = NULL,		\
			.custom_handler = NULL,	\
		},							\
		.num_endpoints = ARRAY_SIZE(gfake_ep_data_##x),		\
		.endpoint = gfake_ep_data_##x,				\
	}

static void google_fake_interface_config(struct usb_desc_header *head,
					 uint8_t bInterfaceNumber)
{
	struct usb_if_descriptor *if_desc = (struct usb_if_descriptor *)head;
	struct usb_google_fake_config *desc =
		CONTAINER_OF(if_desc, struct usb_google_fake_config, if0);

	desc->if0.bInterfaceNumber = bInterfaceNumber;
}

LISTIFY(USB_GFAKE_DEVICE_COUNT, DEFINE_GFAKE_DESCR, (;), _);
LISTIFY(USB_GFAKE_DEVICE_COUNT, DEFINE_GFAKE_EP, (;), _);
LISTIFY(USB_GFAKE_DEVICE_COUNT, DEFINE_GFAKE_CFG_DATA, (;), _);
