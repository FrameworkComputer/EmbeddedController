/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Pericom PI3USB3281 USB port switch driver.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"
#include "pi3usb9281.h"
#include "util.h"

 /* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* 8-bit I2C address */
#define PI3USB9281_I2C_ADDR (0x25 << 1)

/* Delay values */
#define PI3USB9281_SW_RESET_DELAY 20

#ifdef CONFIG_USB_SWITCH_PI3USB9281_MUX_GPIO
#define PI3USB9281_COUNT 2
static inline void select_chip(uint8_t chip_idx)
{
	gpio_set_level(CONFIG_USB_SWITCH_PI3USB9281_MUX_GPIO, chip_idx);
}
#else
#define PI3USB9281_COUNT 1
#define select_chip(x)
#endif

static int saved_interrupts[PI3USB9281_COUNT];

uint8_t pi3usb9281_read(uint8_t chip_idx, uint8_t reg)
{
	int res, val;

	select_chip(chip_idx);
	res = i2c_read8(I2C_PORT_MASTER, PI3USB9281_I2C_ADDR, reg, &val);
	if (res)
		return 0xee;

	return val;
}

int pi3usb9281_write(uint8_t chip_idx, uint8_t reg, uint8_t val)
{
	int res;

	select_chip(chip_idx);
	res = i2c_write8(I2C_PORT_MASTER, PI3USB9281_I2C_ADDR, reg, val);
	if (res)
		CPRINTS("PI3USB9281 I2C write failed");
	return res;
}

int pi3usb9281_enable_interrupts(uint8_t chip_idx)
{
	int ctrl = pi3usb9281_read(chip_idx, PI3USB9281_REG_CONTROL);

	if (ctrl == 0xee)
		return EC_ERROR_UNKNOWN;

	return pi3usb9281_write(chip_idx, PI3USB9281_REG_CONTROL, ctrl & 0x14);
}

int pi3usb9281_disable_interrupts(uint8_t chip_idx)
{
	int ctrl = pi3usb9281_read(chip_idx, PI3USB9281_REG_CONTROL);
	int rv;

	if (ctrl == 0xee)
		return EC_ERROR_UNKNOWN;

	rv = pi3usb9281_write(chip_idx, PI3USB9281_REG_CONTROL,
		(ctrl | PI3USB9281_CTRL_INT_MASK) & 0x15);
	pi3usb9281_get_interrupts(chip_idx);
	return rv;
}

int pi3usb9281_set_interrupt_mask(uint8_t chip_idx, uint8_t mask)
{
	return pi3usb9281_write(chip_idx, PI3USB9281_REG_INT_MASK, ~mask);
}

int pi3usb9281_get_interrupts(uint8_t chip_idx)
{
	int ret = pi3usb9281_peek_interrupts(chip_idx);

	if (chip_idx >= PI3USB9281_COUNT)
		return EC_ERROR_PARAM1;

	saved_interrupts[chip_idx] = 0;
	return ret;
}

int pi3usb9281_peek_interrupts(uint8_t chip_idx)
{
	if (chip_idx >= PI3USB9281_COUNT)
		return EC_ERROR_PARAM1;

	saved_interrupts[chip_idx] |= pi3usb9281_read(chip_idx,
		PI3USB9281_REG_INT);
	return saved_interrupts[chip_idx];
}

int pi3usb9281_get_device_type(uint8_t chip_idx)
{
	return pi3usb9281_read(chip_idx, PI3USB9281_REG_DEV_TYPE) & 0x77;
}

int pi3usb9281_get_charger_status(uint8_t chip_idx)
{
	return pi3usb9281_read(chip_idx, PI3USB9281_REG_CHG_STATUS) & 0x1f;
}

int pi3usb9281_get_ilim(int device_type, int charger_status)
{
	/* Limit USB port current. 500mA for not listed types. */
	int current_limit_ma = 500;

	if (charger_status & PI3USB9281_CHG_CAR_TYPE1 ||
	    charger_status & PI3USB9281_CHG_CAR_TYPE2)
		current_limit_ma = 3000;
	else if (charger_status & PI3USB9281_CHG_APPLE_1A)
		current_limit_ma = 1000;
	else if (charger_status & PI3USB9281_CHG_APPLE_2A)
		current_limit_ma = 2000;
	else if (charger_status & PI3USB9281_CHG_APPLE_2_4A)
		current_limit_ma = 2400;
	else if (device_type & PI3USB9281_TYPE_CDP)
		current_limit_ma = 1500;
	else if (device_type & PI3USB9281_TYPE_DCP)
		current_limit_ma = 500;

	return current_limit_ma;
}

int pi3usb9281_get_vbus(uint8_t chip_idx)
{
	int vbus = pi3usb9281_read(chip_idx, PI3USB9281_REG_VBUS);
	if (vbus == 0xee)
		return EC_ERROR_UNKNOWN;

	return !!(vbus & 0x2);
}

int pi3usb9281_reset(uint8_t chip_idx)
{
	int rv = pi3usb9281_write(chip_idx, PI3USB9281_REG_RESET, 0x1);

	if (!rv)
		/* Reset takes ~15ms. Wait for 20ms to be safe. */
		msleep(PI3USB9281_SW_RESET_DELAY);

	return rv;
}

int pi3usb9281_set_switch_manual(uint8_t chip_idx, int val)
{
	int ctrl;
	int rv;

	ctrl = pi3usb9281_read(chip_idx, PI3USB9281_REG_CONTROL);
	if (ctrl == 0xee)
		return EC_ERROR_UNKNOWN;

	if (val)
		rv = pi3usb9281_write(chip_idx, PI3USB9281_REG_CONTROL,
			ctrl & ~PI3USB9281_CTRL_AUTO);
	else
		rv = pi3usb9281_write(chip_idx, PI3USB9281_REG_CONTROL,
			ctrl | PI3USB9281_CTRL_AUTO);

	return rv;
}

int pi3usb9281_set_pins(uint8_t chip_idx, uint8_t val)
{
	return pi3usb9281_write(chip_idx, PI3USB9281_REG_MANUAL, val);
}

int pi3usb9281_set_switches(uint8_t chip_idx, int open)
{
	uint8_t ctrl = pi3usb9281_read(chip_idx, PI3USB9281_REG_CONTROL) & 0x15;
	if (open)
		ctrl &= ~PI3USB9281_CTRL_SWITCH_AUTO;
	else
		ctrl |= PI3USB9281_CTRL_SWITCH_AUTO;

	return pi3usb9281_write(chip_idx, PI3USB9281_REG_CONTROL, ctrl);
}

static void pi3usb9281_init(void)
{
	uint8_t dev_id;
	int i;

	for (i = 0; i < PI3USB9281_COUNT; i++) {
		dev_id = pi3usb9281_read(i, PI3USB9281_REG_DEV_ID);

		if (dev_id != 0x10)
			CPRINTS("PI3USB9281[%d] invalid ID 0x%02x", i, dev_id);
	}
}
DECLARE_HOOK(HOOK_INIT, pi3usb9281_init, HOOK_PRIO_LAST);
