/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Pericom PI3USB30532 USB port switch driver.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "pi3usb30532.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

#define INTERNAL_READ_ERROR(code) (EC_ERROR_INTERNAL_FIRST + code)
#define IS_READ_ERROR(code) ((code) > EC_ERROR_INTERNAL_FIRST)

/* 8-bit I2C address */
static const int pi3usb30532_addrs[] = CONFIG_USB_SWITCH_I2C_ADDRS;

int pi3usb30532_read(uint8_t chip_idx, uint8_t reg)
{
	int res, val;
	int addr = pi3usb30532_addrs[chip_idx];

	res = i2c_read8(I2C_PORT_USB_SWITCH, addr, reg, &val);
	if (res)
		return INTERNAL_READ_ERROR(res);

	return val;
}

int pi3usb30532_write(uint8_t chip_idx, uint8_t reg, uint8_t val)
{
	int res;
	int addr = pi3usb30532_addrs[chip_idx];

	res = i2c_write8(I2C_PORT_USB_SWITCH, addr, reg, val);
	if (res)
		CPUTS("PI3USB30532 I2C write failed");

	return res;
}

/* Writes control register to set switch mode */
int pi3usb30532_set_switch(uint8_t chip_idx, uint8_t mode)
{
	return pi3usb30532_write(chip_idx, PI3USB30532_REG_CONTROL,
				 (mode & PI3USB30532_CTRL_MASK) |
				 PI3USB30532_CTRL_RSVD);
}

int pi3usb30532_reset(uint8_t chip_idx)
{
	return pi3usb30532_set_switch(chip_idx, PI3USB30532_MODE_POWERDOWN);
}

static void pi3usb30532_init(void)
{
	int i, res, val;

	for (i = 0; i < ARRAY_SIZE(pi3usb30532_addrs); i++) {
		res = pi3usb30532_reset(i);
		if (res)
			CPRINTS("PI3USB30532 [%d] init failed", i);

		val = pi3usb30532_read(i, PI3USB30532_REG_VENDOR);
		if (IS_READ_ERROR(val)) {
			CPRINTS("PI3USB30532 [%d] read failed", i);
			continue;
		}

		if (val != PI3USB30532_VENDOR_ID)
			CPRINTS("PI3USB30532 [%d] invalid ID 0x%02x", i, val);

	}
}
DECLARE_HOOK(HOOK_INIT, pi3usb30532_init, HOOK_PRIO_LAST);
