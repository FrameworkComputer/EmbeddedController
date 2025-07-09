/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __USBD_INIT_H
#define __USBD_INIT_H

#include "hooks.h"
#include "queue.h"

#include <stdint.h>
#include <stdlib.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/usb/usbd.h>

#define USB_SUBCLASS_GOOGLE_FAKE 0xFF
#define USB_PROTOCOL_GOOGLE_FAKE 0xFF

#define GOOGLE_EP_FS_MPS 64

#define AUTO_EP_IN 0x80
#define AUTO_EP_OUT 0x00

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
		.wMaxPacketSize = sys_cpu_to_le16(mps), .bInterval = 1, \
	}

struct hid_dev_t {
	const struct device *dev;
	atomic_t state;
	uint8_t report_protocol;

	struct queue const report_queue;
	struct k_mutex *report_queue_mutex;
};

struct google_desc {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor out_ep;
	struct usb_ep_descriptor in_ep;
} __packed;

struct google_data {
	struct usbd_class_data *c_data;

	struct google_desc *const desc;
	const struct usb_desc_header **const fs_desc;
	atomic_t state;
	struct k_thread tx_thread_data;
	struct k_thread rx_thread_data;
	struct k_sem sync_sem;

	struct k_work output_work;
};

enum {
	HID_CLASS_IFACE_READY = 0,
	HID_CLASS_SUSPENDED,
	HID_EP_IN_BUSY,
};

enum {
	GVENDOR_DEV_ENABLED = 0,
	GVENDOR_DEV_OUT_BUSY,
};

struct usb_msg_manager {
	struct deferred_data *deferred;
	int callback_count;
	int max_callbacks;
};

extern struct usb_msg_manager msg_manager;
extern enum usbd_msg_type usb_message;

int request_usb_wake(void);
int usb_msg_deferred_register(const struct deferred_data *deferred);
uint8_t google_get_in_ep(struct usbd_class_data *const c_data);
uint8_t google_get_out_ep(struct usbd_class_data *const c_data);

#endif /* __USBD_INIT_H */
