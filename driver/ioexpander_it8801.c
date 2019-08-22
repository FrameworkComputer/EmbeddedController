/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "driver/ioexpander_it8801.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"

#define CPRINTS(format, args...) cprints(CC_KEYSCAN, format, ## args)

static int it8801_read(int reg, int *data)
{
	return i2c_read8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_I2C_ADDR,
		reg, data);
}

static int it8801_write(int reg, int data)
{
	return i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_I2C_ADDR,
		reg, data);
}

struct it8801_vendor_id_t {
	uint8_t chip_id;
	uint8_t reg;
};

static const struct it8801_vendor_id_t it8801_vendor_id_verify[] = {
	{ 0x12, IT8801_REG_HBVIDR},
	{ 0x83, IT8801_REG_LBVIDR},
};

static int it8801_check_vendor_id(void)
{
	int i, ret, val;

	/*  Verify vendor ID registers(16-bits). */
	for (i = 0; i < ARRAY_SIZE(it8801_vendor_id_verify); i++) {
		ret = it8801_read(it8801_vendor_id_verify[i].reg, &val);

		if (ret != EC_SUCCESS)
			return ret;

		if (val != it8801_vendor_id_verify[i].chip_id)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void keyboard_raw_init(void)
{
	int ret, val;

	/*  Verify Vendor ID registers. */
	ret = it8801_check_vendor_id();
	if (ret) {
		CPRINTS("Failed to read IT8801 vendor id %x", ret);
		return;
	}

	/* KSO alternate function switching(KSO[21:20, 18]). */
	it8801_write(IT8801_REG_GPIO01_KSO18, IT8801_REG_MASK_GPIOAFS_FUNC2);
	it8801_write(IT8801_REG_GPIO22_KSO21, IT8801_REG_MASK_GPIOAFS_FUNC2);
	it8801_write(IT8801_REG_GPIO23_KSO20, IT8801_REG_MASK_GPIOAFS_FUNC2);

	/* Start with KEYBOARD_COLUMN_ALL, KSO[22:11, 6:0] output low. */
	it8801_write(IT8801_REG_KSOMCR, IT8801_REG_MASK_AKSOSC);

	if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
		/*
		 * Since most of the KSO pins can't drive up, we'll must use
		 * a pin capable of being a GPIO instead and use the GPIO
		 * feature to do the required inverted push pull.
		 */
		it8801_write(IT8801_REG_GPIO23_KSO20, IT8801_REG_MASK_GPIODIR);

		/* Start with KEYBOARD_COLUMN_ALL, output high(so selected). */
		it8801_read(IT8801_REG_GPIOG2SOVR, &val);
		it8801_write(IT8801_REG_GPIOG2SOVR, val | IT8801_REG_GPIO23SOV);
	}

	/* Keyboard scan in interrupt enable register */
	it8801_write(IT8801_REG_KSIIER, 0xff);
	/* Gather KSI interrupt enable */
	it8801_write(IT8801_REG_GIECR, IT8801_REG_MASK_GKSIIE);
	/* Alert response enable */
	it8801_write(IT8801_REG_SMBCR, IT8801_REG_MASK_ARE);

	keyboard_raw_enable_interrupt(0);
}

void keyboard_raw_task_start(void)
{
	keyboard_raw_enable_interrupt(1);
}

static const uint8_t kso_mapping[] = {
	0, 1, 20, 3, 4, 5, 6, 17, 18, 16, 15, 11, 12,
#ifdef CONFIG_KEYBOARD_KEYPAD
	13, 14
#endif
};
BUILD_ASSERT(ARRAY_SIZE(kso_mapping) == KEYBOARD_COLS_MAX);

test_mockable void keyboard_raw_drive_column(int col)
{
	int kso_val, val;

	/* Tri-state all outputs */
	if (col == KEYBOARD_COLUMN_NONE) {
		/* KSO[22:11, 6:0] output high */
		kso_val = IT8801_REG_MASK_KSOSDIC | IT8801_REG_MASK_AKSOSC;

		if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
			/* Output low(so not selected). */
			it8801_read(IT8801_REG_GPIOG2SOVR, &val);
			it8801_write(IT8801_REG_GPIOG2SOVR, val &
				~IT8801_REG_GPIO23SOV);
		}
	}
	/* Assert all outputs */
	else if (col == KEYBOARD_COLUMN_ALL) {
		/* KSO[22:11, 6:0] output low */
		kso_val = IT8801_REG_MASK_AKSOSC;

		if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
			/* Output high(so selected). */
			it8801_read(IT8801_REG_GPIOG2SOVR, &val);
			it8801_write(IT8801_REG_GPIOG2SOVR, val |
				IT8801_REG_GPIO23SOV);
		}
	} else {
		/* To check if column is valid or not. */
		if (col >= KEYBOARD_COLS_MAX)
			return;
		/*
		 * Selected KSO[20, 18:11, 6:3, 1:0] output low,
		 * all others KSO output high.
		 */
		kso_val = kso_mapping[col];

		if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
			/* GPIO23 is inverted. */
			if (col == IT8801_REG_MASK_SELKSO2) {
				/* Output high(so selected). */
				it8801_read(IT8801_REG_GPIOG2SOVR, &val);
				it8801_write(IT8801_REG_GPIOG2SOVR, val |
					IT8801_REG_GPIO23SOV);
			} else {
				/* Output low(so not selected). */
				it8801_read(IT8801_REG_GPIOG2SOVR, &val);
				it8801_write(IT8801_REG_GPIOG2SOVR, val &
					~IT8801_REG_GPIO23SOV);
			}
		}
	}

	it8801_write(IT8801_REG_KSOMCR, kso_val);
}

test_mockable int keyboard_raw_read_rows(void)
{
	int data = 0;
	int ksieer = 0;

	it8801_read(IT8801_REG_KSIDR, &data);

	/* This register needs to write clear after reading data */
	it8801_read(IT8801_REG_KSIEER, &ksieer);
	it8801_write(IT8801_REG_KSIEER, ksieer);

	/* Bits are active-low, so invert returned levels */
	return (~data) & 0xff;
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (enable) {
		it8801_write(IT8801_REG_KSIEER, 0xff);
		gpio_clear_pending_interrupt(GPIO_IT8801_SMB_INT);
		gpio_enable_interrupt(GPIO_IT8801_SMB_INT);
	} else {
		gpio_disable_interrupt(GPIO_IT8801_SMB_INT);
	}
}

void io_expander_it8801_interrupt(enum gpio_signal signal)
{
	/* Wake the scan task */
	task_wake(TASK_ID_KEYSCAN);
}

static void dump_register(int reg)
{
	int rv;
	int data;

	ccprintf("[%Xh] = ", reg);

	rv = it8801_read(reg, &data);

	if (!rv)
		ccprintf("0x%02x\n", data);
	else
		ccprintf("ERR (%d)\n", rv);
}

static int it8801_dump(int argc, char **argv)
{
	dump_register(IT8801_REG_KSIIER);
	dump_register(IT8801_REG_KSIEER);
	dump_register(IT8801_REG_KSIDR);
	dump_register(IT8801_REG_KSOMCR);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(it8801_dump, it8801_dump, "NULL",
			"Dumps IT8801 registers");
