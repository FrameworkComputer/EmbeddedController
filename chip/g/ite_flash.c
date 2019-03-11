/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ccd_config.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "hooks.h"
#include "i2c.h"
#include "init_chip.h"
#include "ite_sync.h"
#include "registers.h"
#include "scratch_reg1.h"
#include "system.h"
#include "timer.h"
#include "usb_i2c.h"

#define ITE_SYNC_TIME  (50 * MSEC)
#define ITE_PERIOD_TIME 5  /* This is 200 kHz */
#define TIMEUS_CLK_FREQ 24 /* units: MHz */
#define HALF_PERIOD_TICKS 8

/* Register controlling CPU clock mode among other things. */
#define PROC_CONTROL_REGISTER 0x4009A6D0

void generate_ite_sync(void)
{
	uint16_t *gpio_addr;
	uint32_t cycle_count;
	uint16_t both_zero;
	uint16_t both_one;
	uint16_t one_zero;
	uint16_t zero_one;
	uint32_t saved_setting;

	/* Let's pulse EC reset while preparing to sync up. */
	assert_ec_rst();
	msleep(1);
	deassert_ec_rst();
	msleep(5);

	/*
	 * Values to write to set SCL and SDA to various combinations of 0 and
	 * 1 to be able to generate two necessary waveforms.
	 */
	both_zero = 0;
	one_zero = BIT(13);
	zero_one = BIT(12);
	both_one = one_zero | zero_one;

	/* Address of the mask byte register to use to set both pins. */
	gpio_addr = (uint16_t *) (GC_GPIO0_BASE_ADDR +
				  GC_GPIO_MASKHIGHBYTE_800_OFFSET +
				  (both_one >> 8) * 4);

	/*
	 * Let's take over the i2c master pins. Connect pads DIOB0(aka i2c
	 * scl) to gpio0.12 and DIOB1(aka sda) to gpio0.13. I2c master
	 * controller is disconnected from the pads.
	 */
	REG32(GBASE(PINMUX) + GOFFSET(PINMUX, DIOB0_SEL)) =
		GC_PINMUX_GPIO0_GPIO12_SEL;
	REG32(GBASE(PINMUX) + GOFFSET(PINMUX, DIOB1_SEL)) =
		GC_PINMUX_GPIO0_GPIO13_SEL;

	gpio_set_flags(GPIO_I2C_SCL_INA, GPIO_OUTPUT | GPIO_HIGH);
	gpio_set_flags(GPIO_I2C_SDA_INA, GPIO_OUTPUT | GPIO_HIGH);

	cycle_count = 2 * ITE_SYNC_TIME / ITE_PERIOD_TIME;

	interrupt_disable();

	init_jittery_clock_locking_optional(1, 0, 0);

	saved_setting = REG32(0x4009A6D0);
	REG32(0x4009A6D0) = 0;

	/* Call assembler function to generate ITE SYNC sequence. */
	ite_sync(gpio_addr, both_zero, one_zero, zero_one, both_one,
		 HALF_PERIOD_TICKS, HALF_PERIOD_TICKS * cycle_count);

	REG32(0x4009A6D0) = saved_setting;

	interrupt_enable();

	/*
	 * Restore I2C configuration, re-attach i2c master controller to the
	 * pads.
	 */
	REG32(GBASE(PINMUX) + GOFFSET(PINMUX, DIOB0_SEL)) =
		GC_PINMUX_I2C0_SCL_SEL;
	REG32(GBASE(PINMUX) + GOFFSET(PINMUX, DIOB1_SEL)) =
		GC_PINMUX_I2C0_SDA_SEL;
}

/*
 * Callback invoked by usb_i2c bridge when a write to a special I2C address is
 * requested.
 */
#define CROS_CMD_ITE_SYNC    0
static int ite_sync_preparer(void *data_in, size_t in_size,
			     void *data_out, size_t out_size)
{

	if (in_size != 1)
		return USB_I2C_WRITE_COUNT_INVALID;

	if (*((uint8_t *)data_in) != CROS_CMD_ITE_SYNC)
		return USB_I2C_UNSUPPORTED_COMMAND;

	if (!ccd_is_cap_enabled(CCD_CAP_EC_FLASH))
		return USB_I2C_DISABLED;

	board_start_ite_sync();

	return 0;
}

static void register_ite_sync(void)
{
	usb_i2c_register_cros_cmd_handler(ite_sync_preparer);
}

DECLARE_HOOK(HOOK_INIT, register_ite_sync, HOOK_PRIO_DEFAULT);
