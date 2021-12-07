/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MPS MP3385 LED driver.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "mp3385.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

#define I2C_ADDR_MP3385_FLAGS	0x31

struct mp3385_value {
	uint8_t offset;
	uint8_t data;
};

/*
 * MP3385 asserts the interrupt when it's ready for writing settings, which
 * are cleared when it's turned off. We enable the interrupt on HOOK_INIT and
 * keep it enabled in S0/S3/S5.
 *
 * It's assumed the device doesn't have a lid and MP3385 is powered only in
 * S0. For clamshell devices, different interrupt & power control scheme may be
 * needed.
 */

/* This ordering is suggested by vendor. */
static struct mp3385_value mp3385_conf[] = {
	/*
	 * Register 0x01: Operation frequency control
	 * Frequency selection: 300(KHz)
	 * Short circuit protection: 8(V)
	 */
	{.offset = 1, .data = 0x43},
	/*
	 * Register 0x02: LED current Full-Scale Register
	 * ISET Resistor: 127(Kohm)
	 * Maximum LED current: 20196/127 = 159(mA)
	 * Setting LED current: 62(mA)
	 */
	{.offset = 2, .data = 0x65},

	 /* Register 0x03: RO - ignored */

	/*
	 * Register 0x04: Internal LED Dimming Brightness Register
	 * SMBus PWM function: None Use
	 */
	{.offset = 4, .data = 0x00},
	/*
	 * Register 0x05: OVP, OCP Threshold Register
	 * Over Current Protection: 0.5(V)
	 * Panel LED Voltage(Max): 47.8(V)
	 * OVP setting: 54(V)
	 */
	{.offset = 5, .data = 0x97},
	/*
	 * Register 0x00: Dimming mode Register
	 * String Selection: 4(Number)
	 * Interface Selection: 1
	 * Brightness mode: 3
	 */
	{.offset = 0, .data = 0xF2},
};
static const int mp3385_conf_size = ARRAY_SIZE(mp3385_conf);

static void set_mp3385_reg(void)
{
	int i;

	for (i = 0; i < mp3385_conf_size; ++i) {
		int rv = i2c_write8(I2C_PORT_BACKLIGHT,
				    I2C_ADDR_MP3385_FLAGS,
				    mp3385_conf[i].offset, mp3385_conf[i].data);
		if (rv) {
			CPRINTS("Write MP3385 register %d "
					"failed rv=%d", i, rv);
			return;
		}
	}
	CPRINTS("Wrote MP3385 settings");
}

static void mp3385_backlight_enable_deferred(void)
{
	if (gpio_get_level(GPIO_PANEL_BACKLIGHT_EN))
		set_mp3385_reg();
}
DECLARE_DEFERRED(mp3385_backlight_enable_deferred);

void backlight_enable_interrupt(enum gpio_signal signal)
{
	/*
	 * 1. Spec says backlight should be turned on after 500ms
	 *    after eDP signals are ready.
	 *
	 * 2. There's no way to get exact eDP ready time, therefore,
	 *    give one second delay.
	 *
	 * power up  __/----------------
	 * eDP       ______/------------
	 * backlight _____________/-----
	 *                 |- t1 -| : >=500 ms
	 *             |-   t2   -| : 1 second is enough
	 */
	hook_call_deferred(&mp3385_backlight_enable_deferred_data,
						MP3385_POWER_BACKLIGHT_DELAY);
}

int mp3385_set_config(int offset, int data)
{
	int i;

	for (i = 0; i < mp3385_conf_size; i++) {
		if (mp3385_conf[i].offset == offset) {
			mp3385_conf[i].data = data;
			return EC_SUCCESS;
		}
	}

	CPRINTS("mp3385: offset %d not found", i);
	return EC_ERROR_INVAL;
}
