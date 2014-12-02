/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "usb_spi.h"

void usb_spi_ready(struct usb_spi_config const *config)
{
	task_wake(TASK_ID_USB_SPI);
}

USB_SPI_CONFIG(usb_spi, USB_IFACE_SPI, USB_EP_SPI, usb_spi_ready)

void usb_spi_task(void)
{
	usb_spi_enable(&usb_spi. 1);

	while (1) {
		task_wait_event(-1);

		while (usb_spi_service_request(&usb_spi))
			;
	}
}
