/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/usb_stream.h"
#include "hooks.h"
#include "queue.h"
#include "task.h"
#include "usb_dc.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>

#include <usb_descriptor.h>

LOG_MODULE_REGISTER(usb_google_update, LOG_LEVEL_INF);

#define AUTO_EP_IN 0x80
#define AUTO_EP_OUT 0x00

#define RX_POOL_COUNT \
	(CONFIG_PLATFORM_EC_UPDATE_PDU_SIZE / USB_MAX_FS_BULK_MPS) + 1

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE
#define TX_POOL_COUNT \
	(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE / USB_MAX_FS_BULK_MPS) + 1
#else
#define TX_POOL_COUNT 2
#endif /* CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE */

NET_BUF_POOL_FIXED_DEFINE(update_rx_pool, RX_POOL_COUNT, USB_MAX_FS_BULK_MPS, 0,
			  NULL);
NET_BUF_POOL_FIXED_DEFINE(update_tx_pool, TX_POOL_COUNT, USB_MAX_FS_BULK_MPS, 0,
			  NULL);

static K_KERNEL_STACK_DEFINE(rx_thread_stack,
			     CONFIG_GOOGLE_UPDATE_RX_STACK_SIZE);
static struct k_thread rx_thread_data;
static K_KERNEL_STACK_DEFINE(tx_thread_stack,
			     CONFIG_GOOGLE_UPDATE_TX_STACK_SIZE);
static struct k_thread tx_thread_data;

static K_FIFO_DEFINE(rx_queue);
static K_FIFO_DEFINE(tx_queue);

enum google_update_ep_index {
	OUT_EP_IDX = 0,
	IN_EP_IDX,
	EP_NUM,
};

struct usb_google_update_config {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
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

/* Coreboot only parses the first interface descriptor for boot keyboard
 * detection. And the USB descriptors are sorted by name in the linker
 * scripts. The string "gupdate" is set in the instance field to ensure
 * that the Google update descriptor is placed after the HID class.
 */
USBD_CLASS_DESCR_DEFINE(primary, gupdate)
struct usb_google_update_config google_update_cfg = {
	.if0 = INITIALIZER_IF(EP_NUM, USB_BCC_VENDOR,
			      USB_SUBCLASS_GOOGLE_UPDATE,
			      USB_PROTOCOL_GOOGLE_UPDATE),
	.if0_out_ep = INITIALIZER_IF_EP(AUTO_EP_OUT, USB_DC_EP_BULK,
					USB_MAX_FS_BULK_MPS),
	.if0_in_ep = INITIALIZER_IF_EP(AUTO_EP_IN, USB_DC_EP_BULK,
				       USB_MAX_FS_BULK_MPS),
};

static struct usb_ep_cfg_data ep_cfg[] = {
	[OUT_EP_IDX] = {
		.ep_cb = usb_transfer_ep_callback,
		.ep_addr = AUTO_EP_OUT,
	},
	[IN_EP_IDX] = {
		.ep_cb = usb_transfer_ep_callback,
		.ep_addr = AUTO_EP_IN,
	},
};

static void google_update_read(uint8_t ep, int size, void *priv)
{
	ARG_UNUSED(priv);

	static uint8_t data[USB_MAX_FS_BULK_MPS];

	if (size > 0) {
		struct net_buf *buf;

		buf = net_buf_alloc(&update_rx_pool, K_NO_WAIT);
		if (!buf) {
			LOG_ERR("failed to allocate memory");
			return;
		}
		net_buf_add_mem(buf, data, size);
		k_fifo_put(&rx_queue, buf);
	}

	/* Start a new read transfer */
	usb_transfer(ep_cfg[OUT_EP_IDX].ep_addr, data, USB_MAX_FS_BULK_MPS,
		     USB_TRANS_READ, google_update_read, NULL);
}

static void google_update_status_cb(struct usb_cfg_data *cfg,
				    enum usb_dc_status_code status,
				    const uint8_t *param)
{
	ARG_UNUSED(cfg);
	ARG_UNUSED(param);

	switch (status) {
	case USB_DC_CONFIGURED:
		LOG_DBG("USB device configured");
		google_update_read(ep_cfg[OUT_EP_IDX].ep_addr, 0, NULL);
		break;
	default:
		break;
	}
}

void usb_update_stream_written(struct consumer const *consumer, size_t count)
{
	static uint8_t data[USB_MAX_FS_BULK_MPS];
	struct net_buf *buf;

	if (queue_is_empty(consumer->queue)) {
		LOG_ERR("consumer queue is empty");
		return;
	}

	do {
		count = (count > USB_MAX_FS_BULK_MPS) ? USB_MAX_FS_BULK_MPS :
							count;
		queue_peek_units(consumer->queue, data, 0, count);
		buf = net_buf_alloc(&update_tx_pool, K_NO_WAIT);
		if (!buf) {
			LOG_ERR("failed to allocate tx memory");
			return;
		}

		net_buf_add_mem(buf, data, count);
		k_fifo_put(&tx_queue, buf);
		queue_advance_head(consumer->queue, count);
		count = queue_count(consumer->queue);
	} while (count != 0);
}

static void google_update_interface_config(struct usb_desc_header *head,
					   uint8_t bInterfaceNumber)
{
	ARG_UNUSED(head);

	google_update_cfg.if0.bInterfaceNumber = bInterfaceNumber;
}

static void google_update_tx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct net_buf *buf;

		buf = k_fifo_get(&tx_queue, K_FOREVER);
		LOG_HEXDUMP_DBG(buf->data, buf->len, "Tx:");

		usb_transfer_sync(ep_cfg[IN_EP_IDX].ep_addr, buf->data,
				  buf->len, USB_TRANS_WRITE | USB_TRANS_NO_ZLP);

		net_buf_unref(buf);
	}
}

static void google_update_rx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct net_buf *buf;
		const struct queue *usb_to_update = usb_update.producer.queue;

		buf = k_fifo_get(&rx_queue, K_FOREVER);
		if (buf->len > queue_space(usb_to_update)) {
			LOG_ERR("queue is full");
			continue;
		}
		queue_add_units(usb_to_update, buf->data, buf->len);
		LOG_HEXDUMP_DBG(buf->data, buf->len, "Rx:");
		net_buf_unref(buf);
	}
}

static int usb_google_update_init(void)
{
	k_thread_create(&rx_thread_data, rx_thread_stack,
			K_KERNEL_STACK_SIZEOF(rx_thread_stack),
			google_update_rx_thread, NULL, NULL, NULL,
			K_PRIO_COOP(CONFIG_GOOGLE_UPDATE_RX_THREAD_PRIORTY), 0,
			K_NO_WAIT);

	k_thread_name_set(&rx_thread_data, "gupdate_rx");

	k_thread_create(&tx_thread_data, tx_thread_stack,
			K_KERNEL_STACK_SIZEOF(tx_thread_stack),
			google_update_tx_thread, NULL, NULL, NULL,
			K_PRIO_COOP(CONFIG_GOOGLE_UPDATE_TX_THREAD_PRIORTY), 0,
			K_NO_WAIT);

	k_thread_name_set(&tx_thread_data, "gupdate_tx");

	return 0;
}

SYS_INIT(usb_google_update_init, APPLICATION,
	 CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

USBD_DEFINE_CFG_DATA(google_update_config) = {
	.usb_device_description = NULL,
	.interface_config = google_update_interface_config,
	.interface_descriptor = &google_update_cfg.if0,
	.cb_usb_status = google_update_status_cb,
	.interface = {
		.class_handler = NULL,
		.custom_handler = NULL,
		.vendor_handler = NULL,
	},
	.num_endpoints = ARRAY_SIZE(ep_cfg),
	.endpoint = ep_cfg,
};
