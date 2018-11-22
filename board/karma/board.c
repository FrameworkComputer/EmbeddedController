/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)

#define I2C_ADDR_OZ554		0x62
#define OZ554_DATA_SIZE		6

struct oz554_value {
	uint8_t offset;
	uint8_t data;
};

/* This ordering is suggested by vendor. */
static const struct oz554_value oz554_order[] = {
	/*
	 * Reigster 0x01: Operation frequency control
	 * Frequency selection: 300(KHz)
	 * Short circuit protection: 8(V)
	 */
	{.offset = 1, .data = 0x43},
	/*
	 * Reigster 0x02: LED current amplitude control
	 * ISET Resistor: 10.2(Kohm)
	 * Maximum LED current: 1636/10.2 = 160.4(mA)
	 * Setting LED current: 65(mA)
	 */
	{.offset = 2, .data = 0x65},
	/*
	 * Reigster 0x03: LED backlight Status
	 * Status function: Read only
	 */
	{.offset = 3, .data = 0x00},
	/*
	 * Reigster 0x04: LED current control with SMBus
	 * SMBus PWM function: None Use
	 */
	{.offset = 4, .data = 0x00},
	/*
	 * Reigster 0x05: OVP, OCP control
	 * Over Current Protection: 0.5(V)
	 * Panel LED Voltage(Max): 47.8(V)
	 * OVP setting: 54(V)
	 */
	{.offset = 5, .data = 0x97},
	/*
	 * Reigster 0x00: Dimming mode and string ON/OFF control
	 * String Selection: 4(Number)
	 * Interface Selection: 1
	 * Brightness mode: 3
	 */
	{.offset = 0, .data = 0xF2},
};
BUILD_ASSERT(ARRAY_SIZE(oz554_order) == OZ554_DATA_SIZE);

static void set_oz554_reg(void)
{
	int i, rv;

	for (i = 0; i < OZ554_DATA_SIZE; ++i) {
		rv = i2c_write8(
			NPCX_I2C_PORT1,
			I2C_ADDR_OZ554,
			oz554_order[i].offset,
			oz554_order[i].data);

		if (rv) {
			CPRINTS("Write OZ554 register index %d failed, rv = %d"
				, i, rv);
			break;
		}
	}
}

static void backlight_enable_deferred(void)
{
	if (gpio_get_level(GPIO_PANEL_BACKLIGHT_EN))
		set_oz554_reg();
}
DECLARE_DEFERRED(backlight_enable_deferred);

void backlight_enable_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&backlight_enable_deferred_data, 30 * MSEC);
}

static void karma_chipset_resume(void)
{
	/* Enable panel backlight interrupt. */
	gpio_enable_interrupt(GPIO_PANEL_BACKLIGHT_EN);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, karma_chipset_resume, HOOK_PRIO_DEFAULT);

static void karma_chipset_shutdown(void)
{
	/* Disable panel backlight interrupt. */
	gpio_disable_interrupt(GPIO_PANEL_BACKLIGHT_EN);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, karma_chipset_shutdown, HOOK_PRIO_DEFAULT);
