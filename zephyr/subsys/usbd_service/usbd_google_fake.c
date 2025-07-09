/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT zephyr_hid_device

#include "usbd_init.h"

#include <zephyr/logging/log.h>

#include <usb_descriptor.h>

LOG_MODULE_REGISTER(usbd_google_fake, LOG_LEVEL_INF);

static void usbd_gfake_enable(struct usbd_class_data *const c_data)
{
	LOG_DBG("configuration enabled");
}

static void usbd_gfake_disable(struct usbd_class_data *const c_data)
{
	LOG_DBG("configuration disabled");
}

static void *usbd_gfake_get_desc(struct usbd_class_data *const c_data,
				 const enum usbd_speed speed)
{
	const struct device *dev = usbd_class_get_private(c_data);
	struct google_data *data = dev->data;

	if (speed == USBD_SPEED_FS) {
		return data->fs_desc;
	}

	LOG_ERR("Gfake only supports full-speed");

	return NULL;
}

static int usbd_gfake_init(struct usbd_class_data *const c_data)
{
	LOG_DBG("Google class %s init", c_data->name);

	return 0;
}

static int gfake_device_init(const struct device *dev)
{
	LOG_DBG("Gfake device %s init", dev->name);

	return 0;
}

struct usbd_class_api gfake_api = {
	.request = NULL,
	.enable = usbd_gfake_enable,
	.disable = usbd_gfake_disable,
	.get_desc = usbd_gfake_get_desc,
	.init = usbd_gfake_init,
};

#define GFAKE_INTERFACE_DEFINE(n)                                             \
	static struct google_desc gfake_desc_##n = {                          \
		.if0 = INITIALIZER_IF(1, USB_BCC_VENDOR,                      \
				      USB_SUBCLASS_GOOGLE_FAKE,               \
				      USB_PROTOCOL_GOOGLE_FAKE),              \
		.in_ep = INITIALIZER_IF_EP(AUTO_EP_IN, USB_EP_TYPE_INTERRUPT, \
					   GOOGLE_EP_FS_MPS),                 \
	};                                                                    \
                                                                              \
	const static struct usb_desc_header *gfake_fs_desc_##n[] = {          \
		(struct usb_desc_header *)&gfake_desc_##n.if0,                \
		(struct usb_desc_header *)&gfake_desc_##n.in_ep,              \
		NULL,                                                         \
	}

#define USBD_GFAKE_INSTANCE_DEFINE(n)                                         \
	GFAKE_INTERFACE_DEFINE(n);                                            \
                                                                              \
	USBD_DEFINE_CLASS(vendor_gfake_##n, &gfake_api,                       \
			  (void *)DEVICE_DT_GET(DT_DRV_INST(n)), NULL);       \
                                                                              \
	static struct google_data gfake_data_##n = {                          \
		.sync_sem = Z_SEM_INITIALIZER(gfake_data_##n.sync_sem, 0, 1), \
		.desc = &gfake_desc_##n,                                      \
		.fs_desc = gfake_fs_desc_##n,                                 \
	};                                                                    \
                                                                              \
	DEVICE_DT_INST_DEFINE(n, gfake_device_init, NULL, &gfake_data_##n,    \
			      NULL, POST_KERNEL,                              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(USBD_GFAKE_INSTANCE_DEFINE);
