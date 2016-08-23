/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Cypress HX3 USB Hub configuration */

#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "usb_descriptor.h"
#include "util.h"

/* Cypress HX3 I2C address */
#define HX3_I2C_ADDR (0x60 << 1)

/* Full size setting blob */
#define HX3_SETTINGS_SIZE 192

/* USB PID assigned the HX3 USB Hub */
#define USB_PID_HUB 0x5016

/* represent a 16-bit integer as 2 uint8_t in little endian */
#define U16(n)  ((n) & 0xff), ((n) >> 8)

/* Cypress HX3 hub settings blob */
const uint8_t hx3_settings[5 + HX3_SETTINGS_SIZE] = {
	 'C', 'Y', /* Cypress magic signature */
	 0x30, /* I2C speed : 100kHz */
	 0xd4, /* Image type: Only settings, no firmware */
	 HX3_SETTINGS_SIZE, /* 192 bytes payload */
	 U16(USB_VID_GOOGLE), U16(USB_PID_HUB), /* USB VID:PID 0x18d1:0x5016 */
	 U16(0x0100), /* bcdDevice 1.00 */
	 0x00, /* Reserved */
	 0x0f, /* 4 SuperSpeed ports, no shared link */
	 0x32, /* bPwrOn2PwrGood : 100 ms */
	 0xef, /* 4 Downstream ports : DS4 is non-removable (MCU) */
	 0x10,
	 0xa0, /* Suspend indicator disabled, Power switch : active HIGH */
	 0x15, /* BC1.2 + ACA Dock + Ghost charging */
	 0xf0, /* CDP enabled, DCP disabled */
	 0x68,
	 0x00, /* Reserved */
	 0x08, /* USB String descriptors enabled */
	 0x00, 0x00,
	 0x12, 0x00, 0x2c,
	 0x66, 0x66, /* USB3.0 TX driver de-emphasis */
	 0x69, 0x29, 0x29, 0x29, 0x29, /* TX amplitude */
	 0x00, /* Reserved */
	 U16(USB_PID_HUB), /* USB2.0 PID: 0x5016 */
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Reserved */
	 0x04, USB_DT_STRING, 0x09, 0x04, /* LangID = 0x0409 US English */

	 0x18, USB_DT_STRING, /* Manufacturer string descriptor */
	 0x47, 0x00, 0x6f, 0x00, 0x6f, 0x00, 0x67, 0x00, /* Google Inc. */
	 0x6c, 0x00, 0x65, 0x00, 0x20, 0x00, 0x49, 0x00, /* as UTF-8 */
	 0x6e, 0x00, 0x63, 0x00, 0x2e, 0x00,

	 0x1C, USB_DT_STRING, /* Product string descriptor */
	 0x48, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x65, 0x00, /* HoneyBuns Hub */
	 0x79, 0x00, 0x62, 0x00, 0x75, 0x00, 0x6e, 0x00, /* as UTF-8 */
	 0x73, 0x00, 0x20, 0x00, 0x48, 0x00, 0x75, 0x00,
	 0x62, 0x00,

	 0x02, USB_DT_STRING, /* Serial string descriptor : empty */
	 /* Free space for more strings */
};

static int hx3_configured;

static int configure_hx3(void)
{
	int ret;
	int remaining, len;
	uint8_t *data = (uint8_t *)hx3_settings;
	uint16_t addr = 0x0000;
	int success = 1;

	remaining = sizeof(hx3_settings);
	while (remaining && gpio_get_level(GPIO_HUB_RESET_L)) {
		/* do 64-byte Page Write */
		len = MIN(remaining, 64);
		i2c_lock(I2C_PORT_MASTER, 1);
		/* send Page Write address */
		ret = i2c_xfer(I2C_PORT_MASTER, HX3_I2C_ADDR,
			       (uint8_t *)&addr, 2, NULL, 0, I2C_XFER_START);
		/* send page data */
		ret |= i2c_xfer(I2C_PORT_MASTER, HX3_I2C_ADDR,
				data, len, NULL, 0, I2C_XFER_STOP);
		i2c_lock(I2C_PORT_MASTER, 0);
		if (ret != EC_SUCCESS) {
			success = 0;
			ccprintf("HX3 transfer failed %d\n", ret);
			break;
		}
		remaining -= len;
		data += len;
	}
	return gpio_get_level(GPIO_HUB_RESET_L) ? success : 0;
}

void hx3_task(void)
{
	while (1) {
		task_wait_event(-1);
		if (!hx3_configured && gpio_get_level(GPIO_HUB_RESET_L)) {
			/* wait for the HX3 to come out of reset */
			msleep(5);
			hx3_configured = configure_hx3();
		}
	}
}

void hx3_enable(int enable)
{
	/* Release reset when the Hub is enabled */
	gpio_set_level(GPIO_HUB_RESET_L, !!enable);
	/* Trigger I2C configuration if needed */
	if (enable)
		task_wake(TASK_ID_USBCFG);
	else
		hx3_configured = 0;
}

static int command_hx3(int argc, char **argv)
{
	/* Reset the bridge to put it back in bootloader mode */
	hx3_enable(0);
	/* Keep the reset low at least 10ms (same as the RC) */
	msleep(50);
	/* Release reset and wait for the hub to come up */
	hx3_enable(1);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hx3, command_hx3,
			"",
			"Reset and Send HX3 Hub settings over I2C");
