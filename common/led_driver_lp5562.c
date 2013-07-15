/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LP5562 driver.
 */

#include "console.h"
#include "i2c.h"
#include "lp5562.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* 8-bit I2C address */
#define LP5562_I2C_ADDR (0x30 << 1)

inline int lp5562_write(uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_HOST, LP5562_I2C_ADDR, reg, val);
}

inline int lp5562_read(uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_HOST, LP5562_I2C_ADDR, reg, val);
}

int lp5562_set_color(uint32_t rgb)
{
	int ret = 0;

	ret |= lp5562_write(LP5562_REG_B_PWM, rgb & 0xff);
	ret |= lp5562_write(LP5562_REG_G_PWM, (rgb >> 8) & 0xff);
	ret |= lp5562_write(LP5562_REG_R_PWM, (rgb >> 16) & 0xff);

	return ret;
}

int lp5562_set_engine(uint8_t r, uint8_t g, uint8_t b)
{
	return lp5562_write(LP5562_REG_LED_MAP, (r << 4) | (g << 2) | b);
}

int lp5562_engine_load(int engine, const uint8_t *program, int size)
{
	int prog_addr = LP5562_REG_ENG_PROG(engine);
	int i, ret, val;
	int shift = 6 - engine * 2;

	ret = lp5562_read(LP5562_REG_OP_MODE, &val);
	if (ret)
		return ret;
	val &= ~(0x3 << shift);
	val |= 0x1 << shift;
	ret = lp5562_write(LP5562_REG_OP_MODE, val);
	if (ret)
		return ret;

	for (i = 0; i < size; ++i) {
		ret = lp5562_write(prog_addr + i, program[i]);
		if (ret)
			return ret;
	}

	val &= ~(0x3 << shift);
	val |= 0x2 << shift;
	ret = lp5562_write(LP5562_REG_OP_MODE, val);

	return ret;
}

int lp5562_engine_control(int eng1, int eng2, int eng3)
{
	int ret, val;

	ret = lp5562_read(LP5562_REG_ENABLE, &val);
	if (ret)
		return ret;
	val &= 0xc0;
	val |= (eng1 << 4) | (eng2 << 2) | eng3;
	return lp5562_write(LP5562_REG_ENABLE, val);
}

int lp5562_get_engine_state(int engine)
{
	int val;

	if (lp5562_read(LP5562_REG_ENABLE, &val))
		return 0xee;
	return (val >> (6 - engine * 2)) & 0x3;
}

int lp5562_poweron(void)
{
	int ret = 0;

	ret |= lp5562_write(LP5562_REG_ENABLE, 0x40);
	udelay(500); /* start-up delay */

	ret |= lp5562_write(LP5562_REG_CONFIG, 0x1);
	ret |= lp5562_write(LP5562_REG_LED_MAP, 0x0);

	return ret;
}

int lp5562_poweroff(void)
{
	return lp5562_write(LP5562_REG_ENABLE, 0x0);
}

int lp5562_get_pc(int engine)
{
	int ret;
	if (lp5562_read(LP5562_REG_ENG1_PC + engine - 1, &ret))
		return 0xee;
	return ret;
}

int lp5562_set_pc(int engine, int val)
{
	return lp5562_write(LP5562_REG_ENG1_PC + engine - 1, val);
}

/*****************************************************************************/
/* Console commands */

static int command_lp5562(int argc, char **argv)
{
	if (argc == 4) {
		char *e;
		uint8_t red, green, blue;

		red = strtoi(argv[1], &e, 0);
		if (e && *e)
			return EC_ERROR_PARAM1;
		green = strtoi(argv[2], &e, 0);
		if (e && *e)
			return EC_ERROR_PARAM2;
		blue = strtoi(argv[3], &e, 0);
		if (e && *e)
			return EC_ERROR_PARAM3;

		return lp5562_set_color((red << 16) | (green << 8) | blue);
	} else if (argc == 2) {
		int v;

		if (!parse_bool(argv[1], &v))
			return EC_ERROR_PARAM1;

		if (v)
			return lp5562_poweron();
		else
			return lp5562_poweroff();
	}

	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(lp5562, command_lp5562,
			"on | off | <red> <green> <blue>",
			"Set the color of the LED",
			NULL);
