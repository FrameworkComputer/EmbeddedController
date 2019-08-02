/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SILERGY SY21612 buck-boost converter driver.
 */


#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "sy21612.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

static int sy21612_clear_set_reg(int reg, int clear, int set)
{
	int val, old_val, rv;

	rv = i2c_read8(I2C_PORT_SY21612, SY21612_ADDR_FLAGS, reg, &old_val);
	if (rv)
		return rv;

	val = old_val;
	val &= ~clear;
	val |= set;

	if (val != old_val || clear || set)
		rv = i2c_write8(I2C_PORT_SY21612, SY21612_ADDR_FLAGS,
				reg, val);

	return rv;
}

static int sy21612_read(int reg, int *val)
{
	return i2c_read8(I2C_PORT_SY21612, SY21612_ADDR_FLAGS, reg, val);
}

int sy21612_enable_regulator(int enable)
{
	return enable ?
		sy21612_clear_set_reg(SY21612_CTRL1, 0, SY21612_CTRL1_REG_EN) :
		sy21612_clear_set_reg(SY21612_CTRL1, SY21612_CTRL1_REG_EN, 0);
}

int sy21612_enable_adc(int enable)
{
	return enable ?
		sy21612_clear_set_reg(SY21612_CTRL1, 0, SY21612_CTRL1_ADC_EN) :
		sy21612_clear_set_reg(SY21612_CTRL1, SY21612_CTRL1_ADC_EN, 0);
}

int sy21612_set_adc_mode(int auto_mode)
{
	return auto_mode ?
		sy21612_clear_set_reg(SY21612_CTRL1,
				      0, SY21612_CTRL1_ADC_AUTO_MODE) :
		sy21612_clear_set_reg(SY21612_CTRL1,
				      SY21612_CTRL1_ADC_AUTO_MODE, 0);
}

int sy21612_set_vbus_discharge(int auto_discharge)
{
	return auto_discharge ?
		sy21612_clear_set_reg(SY21612_CTRL1,
				      SY21612_CTRL1_VBUS_NDISCHG, 0) :
		sy21612_clear_set_reg(SY21612_CTRL1,
				      0, SY21612_CTRL1_VBUS_NDISCHG);
}

int sy21612_set_switching_freq(enum sy21612_switching_freq freq)
{
	return sy21612_clear_set_reg(SY21612_CTRL2,
				     SY21612_CTRL2_FREQ_MASK,
				     freq << SY21612_CTRL2_FREQ_SHIFT);
}

int sy21612_set_vbus_volt(enum sy21612_vbus_volt volt)
{
	return sy21612_clear_set_reg(SY21612_CTRL2,
				     SY21612_CTRL2_VBUS_MASK,
				     volt << SY21612_CTRL2_VBUS_SHIFT);
}

int sy21612_set_vbus_adj(enum sy21612_vbus_adj adj)
{
	return sy21612_clear_set_reg(SY21612_CTRL2,
				     SY21612_CTRL2_VBUS_ADJ_MASK,
				     adj << SY21612_CTRL2_VBUS_ADJ_SHIFT);
}

int sy21612_set_sink_mode(int sink_mode)
{
	return sink_mode ?
		sy21612_clear_set_reg(SY21612_PROT2,
				      0, SY21612_PROT2_SINK_MODE) :
		sy21612_clear_set_reg(SY21612_PROT2,
				      SY21612_PROT2_SINK_MODE, 0);
}

int sy21612_is_power_good(void)
{
	int reg;

	if (sy21612_read(SY21612_STATE, &reg))
		return 0;

	return reg & SY21612_STATE_POWER_GOOD;
}

int sy21612_read_clear_int(void)
{
	int reg;

	if (sy21612_read(SY21612_INT, &reg))
		return 0;

	return reg;
}

int sy21612_get_vbat_voltage(void)
{
	int reg;

	if (sy21612_read(SY21612_VBAT_VOLT, &reg))
		return 0;

	return reg * 25000 / 255;
}

int sy21612_get_vbus_voltage(void)
{
	int reg;

	if (sy21612_read(SY21612_VBUS_VOLT, &reg))
		return 0;

	return reg * 25000 / 255;
}

int sy21612_get_vbus_current(void)
{
	int reg;

	if (sy21612_read(SY21612_VBUS_CURRENT, &reg))
		return 0;

	/*
	 * delta V in range 0 ~ 67mV
	 * sense resistor 10 mOhm
	 */
	return reg * 6700 / 255;
}

void sy21612_int(enum gpio_signal signal)
{
#ifdef HAS_TASK_SY21612
	task_wake(TASK_ID_SY21612);
#endif
}

#ifdef HAS_TASK_SY21612
void sy21612_task(void)
{
	int flags;

	while (1) {
		task_wait_event(-1);
		if (sy21612_read(SY21612_INT, &flags))
			continue;
		/* TODO: notify the error condition and enable regulator */
		if (flags & SY21612_INT_VBUS_OCP)
			CPUTS("buck-boost VBUS OCP\n");
		if (flags & SY21612_INT_INDUCTOR_OCP)
			CPUTS("buck-boost inductor OCP\n");
		if (flags & SY21612_INT_UVP)
			CPUTS("buck-boost UVP\n");
		if (flags & SY21612_INT_OTP)
			CPUTS("buck-boost OTP\n");
	}
}
#endif

#ifdef CONFIG_CMD_CHARGER
static int command_sy21612(int argc, char **argv)
{
	int i, val, rv;

	ccputs("sy21612 regs:\n");
	for (i = 0; i < 9; i++) {
		ccprintf("[%02x] ", i);
		rv = sy21612_read(i, &val);
		if (rv)
			ccprintf(" x (%d)\n", rv);
		else
			ccprintf("%02x - %pb\n", val, BINARY_VALUE(val, 8));
	}

	ccprintf("vbat voltage: %d mV\n", sy21612_get_vbat_voltage());
	ccprintf("vbus voltage: %d mV\n", sy21612_get_vbus_voltage());
	ccprintf("vbus current: %d mA\n", sy21612_get_vbus_current());

	return 0;
}
DECLARE_CONSOLE_COMMAND(sy21612, command_sy21612,
			NULL, NULL);
#endif

