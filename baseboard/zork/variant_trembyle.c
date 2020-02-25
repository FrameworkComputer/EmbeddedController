/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "driver/retimer/ps8811.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/*****************************************************************************
 * USB-A Retimer tuning
 */
#define PS8811_ACCESS_RETRIES 2

/* PS8811 gain tuning */
static void ps8811_tuning_init(void)
{
	int rv;
	int retry;

	/* Turn on the retimers */
	ioex_set_level(IOEX_USB_A0_RETIMER_EN, 1);
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);

	/* USB-A0 can run with default settings */
	for (retry = 0; retry < PS8811_ACCESS_RETRIES; ++retry) {
		int val;

		rv = i2c_read8(I2C_PORT_USBA0,
				PS8811_I2C_ADDR_FLAGS + PS8811_REG_PAGE1,
				PS8811_REG1_USB_BEQ_LEVEL, &val);
		if (!rv)
			break;
	}
	if (rv) {
		ioex_set_level(IOEX_USB_A0_RETIMER_EN, 0);
		CPRINTSUSB("C0: PS8811 not detected");
	}

	/* USB-A1 needs to increase gain to get over MB/DB connector */
	for (retry = 0; retry < PS8811_ACCESS_RETRIES; ++retry) {
		rv = i2c_write8(I2C_PORT_USBA1,
				PS8811_I2C_ADDR_FLAGS + PS8811_REG_PAGE1,
				PS8811_REG1_USB_BEQ_LEVEL,
				PS8811_BEQ_I2C_LEVEL_UP_13DB |
				PS8811_BEQ_PIN_LEVEL_UP_18DB);
		if (!rv)
			break;
	}
	if (rv) {
		ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
		CPRINTSUSB("C1: PS8811 not detected");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, ps8811_tuning_init, HOOK_PRIO_DEFAULT);

static void ps8811_retimer_off(void)
{
	/* Turn on the retimers */
	ioex_set_level(IOEX_USB_A0_RETIMER_EN, 0);
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, ps8811_retimer_off, HOOK_PRIO_DEFAULT);
