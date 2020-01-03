/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Silicon Mitus SM5803 Buck-Boost Charger
 */
#include "battery_smart.h"
#include "charger.h"
#include "gpio.h"
#include "i2c.h"
#include "sm5803.h"
#include "throttle_ap.h"
#include "usb_charge.h"

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "SM5803 is a NVDC charger, please enable CONFIG_CHARGER_NARROW_VDC."
#endif

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

static const struct charger_info sm5803_charger_info = {
	.name         = CHARGER_NAME,
	.voltage_max  = CHARGE_V_MAX,
	.voltage_min  = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max  = CHARGE_I_MAX,
	.current_min  = CHARGE_I_MIN,
	.current_step = CHARGE_I_STEP,
	.input_current_max  = INPUT_I_MAX,
	.input_current_min  = INPUT_I_MIN,
	.input_current_step = INPUT_I_STEP,
};


static inline enum ec_error_list chg_read8(int chgnum, int offset, int *value)
{
	return i2c_read8(chg_chips[chgnum].i2c_port,
			 chg_chips[chgnum].i2c_addr_flags,
			 offset, value);
}

static inline enum ec_error_list chg_write8(int chgnum, int offset, int value)
{
	return i2c_write8(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags,
			  offset, value);
}

static inline enum ec_error_list meas_read8(int chgnum, int offset, int *value)
{
	return i2c_read8(chg_chips[chgnum].i2c_port,
			 SM5803_ADDR_MEAS_FLAGS,
			 offset, value);
}

static inline enum ec_error_list meas_write8(int chgnum, int offset, int value)
{
	return i2c_write8(chg_chips[chgnum].i2c_port,
			 SM5803_ADDR_MEAS_FLAGS,
			 offset, value);
}

static inline enum ec_error_list main_read8(int chgnum, int offset, int *value)
{
	return i2c_read8(chg_chips[chgnum].i2c_port,
			 SM5803_ADDR_MAIN_FLAGS,
			 offset, value);
}

static inline enum ec_error_list main_write8(int chgnum, int offset, int value)
{
	return i2c_write8(chg_chips[chgnum].i2c_port,
			 SM5803_ADDR_MAIN_FLAGS,
			 offset, value);
}

enum ec_error_list sm5803_configure_gpio0(int chgnum,
					  enum sm5803_gpio0_modes mode)
{
	enum ec_error_list rv;
	int reg;

	rv = main_read8(chgnum, SM5803_REG_GPIO0_CTRL, &reg);
	if (rv)
		return rv;

	reg &= ~SM5803_GPIO0_MODE_MASK;
	reg |= mode << 1;

	rv = main_write8(chgnum, SM5803_REG_GPIO0_CTRL, reg);
	return rv;
}

enum ec_error_list sm5803_set_gpio0_level(int chgnum, int level)
{
	enum ec_error_list rv;
	int reg;

	rv = main_read8(chgnum, SM5803_REG_GPIO0_CTRL, &reg);
	if (rv)
		return rv;

	if (level)
		reg |= SM5803_GPIO0_VAL;
	else
		reg &= ~SM5803_GPIO0_VAL;

	rv = main_write8(chgnum, SM5803_REG_GPIO0_CTRL, reg);
	return rv;
}

static void sm5803_init(int chgnum)
{
	enum ec_error_list rv;
	int reg;

	/* Set default input current */
	reg = SM5803_CURRENT_TO_REG(CONFIG_CHARGER_INPUT_CURRENT)
		& SM5803_CHG_ILIM_RAW;
	rv = chg_write8(chgnum, SM5803_REG_CHG_ILIM, reg);

	/* Configure TINT and Vbus interrupts to fire */
	rv |= main_write8(chgnum, SM5803_REG_INT2_EN, SM5803_INT2_TINT
						      & SM5803_INT2_VBUS);

	/* Set Vbus interrupt levels for 3.5V and 4.0V */
	rv |= meas_write8(chgnum, SM5803_REG_VBUS_LOW_TH,
			  SM5803_VBUS_LOW_LEVEL);
	rv |= meas_write8(chgnum, SM5803_REG_VBUS_HIGH_TH,
			  SM5803_VBUS_HIGH_LEVEL);

	/* Set TINT for 360K (steps are in 0.43K) */
	rv |= meas_write8(chgnum, SM5803_REG_TINT_HIGH_TH, 0xD1);

	if (rv)
		CPRINTS("%s %d: Failed initialization", CHARGER_NAME, chgnum);
}

static enum ec_error_list sm5803_post_init(int chgnum)
{
	/* Nothing to do, charger is always powered */
	return EC_SUCCESS;
}

/*
 * Process interrupt registers and report any Vbus changes.  Pull PROCHOT low if
 * charger has gotten too hot.
 */
void sm5803_handle_interrupt(int chgnum)
{
	enum ec_error_list rv;
	int int_reg, vbus_reg;

	/* Note: Interrupt register is clear on read */
	rv = main_read8(chgnum, SM5803_REG_INT2_REQ, &int_reg);
	if (rv) {
		CPRINTS("%s %d: Failed read int2 register", CHARGER_NAME,
			chgnum);
		return;
	}

	if (int_reg & SM5803_INT2_TINT) {
		CPRINTS("%s %d: High temp, throttling AP", CHARGER_NAME,
			chgnum);
		throttle_ap(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_THERMAL);
	}

	if (int_reg & SM5803_INT2_VBUS) {
		/* TODO(b/146651778): Check whether we're sourcing Vbus */
		rv = meas_read8(chgnum, SM5803_REG_VBUS_MEAS_MSB, &vbus_reg);
		if (vbus_reg <= SM5803_VBUS_LOW_LEVEL)
			usb_charger_vbus_change(chgnum, 0);
		else if (vbus_reg >= SM5803_VBUS_HIGH_LEVEL)
			usb_charger_vbus_change(chgnum, 1);
		else
			CPRINTS("%s %d: Unexpected Vbus interrupt: 0x%02x",
				CHARGER_NAME, chgnum, vbus_reg);
	}
}

static const struct charger_info *sm5803_get_info(int chgnum)
{
	return &sm5803_charger_info;
}

static enum ec_error_list sm5803_get_status(int chgnum, int *status)
{
	enum ec_error_list rv;
	int reg;

	/* Charger obeys smart battery requests - making it level 2 */
	*status = CHARGER_LEVEL_2;

	rv = chg_read8(chgnum, SM5803_REG_FLOW1, &reg);
	if (rv)
		return rv;

	if (!(reg & SM5803_FLOW1_CHG_EN))
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}

static enum ec_error_list sm5803_set_mode(int chgnum, int mode)
{
	enum ec_error_list rv;
	int flow1_reg, flow2_reg;

	rv = chg_read8(chgnum, SM5803_REG_FLOW1, &flow1_reg);
	if (rv)
		return rv;

	rv = chg_read8(chgnum, SM5803_REG_FLOW2, &flow2_reg);
	if (rv)
		return rv;

	if (mode & CHARGE_FLAG_INHIBIT_CHARGE) {
		/* If already inhibited, don't bother re-inhibiting */
		if (!(flow1_reg & SM5803_FLOW1_CHG_EN))
			return EC_SUCCESS;

		flow1_reg &= ~SM5803_FLOW1_CHG_EN;
		flow2_reg &= ~SM5803_FLOW2_AUTO_ENABLED;
	} else {
		flow1_reg |= SM5803_FLOW1_CHG_EN;
		flow2_reg |= SM5803_FLOW2_AUTO_ENABLED;
	}

	rv = chg_write8(chgnum, SM5803_REG_FLOW1, flow1_reg);
	rv |= chg_write8(chgnum, SM5803_REG_FLOW2, flow2_reg);

	return rv;
}

static enum ec_error_list sm5803_get_current(int chgnum, int *current)
{
	enum ec_error_list rv;
	int reg;

	rv = chg_read8(chgnum, SM5803_REG_FAST_CONF4, &reg);
	if (rv)
		return rv;

	*current = SM5803_REG_TO_CURRENT(reg & SM5803_CONF4_ICHG_FAST);
	return EC_SUCCESS;
}

static enum ec_error_list sm5803_set_current(int chgnum, int current)
{
	enum ec_error_list rv;
	int reg;

	rv = chg_read8(chgnum, SM5803_REG_FAST_CONF4, &reg);
	if (rv)
		return rv;

	reg &= ~SM5803_CONF4_ICHG_FAST;
	reg |= SM5803_CURRENT_TO_REG(current);

	rv = chg_write8(chgnum, SM5803_REG_FAST_CONF4, reg);
	return rv;
}

static enum ec_error_list sm5803_get_voltage(int chgnum, int *voltage)
{
	enum ec_error_list rv;
	int reg;
	int volt_bits;

	/* Note: Vsys should match Vbat voltage */
	rv = chg_read8(chgnum, SM5803_REG_VSYS_PREREG_MSB, &reg);
	if (rv)
		return rv;

	volt_bits = reg << 3;

	rv = chg_read8(chgnum, SM5803_REG_VSYS_PREREG_LSB, &reg);
	if (rv)
		return rv;

	volt_bits |= (reg & 0x7);

	*voltage = SM5803_REG_TO_VOLTAGE(volt_bits);
	return EC_SUCCESS;
}

static enum ec_error_list sm5803_set_voltage(int chgnum, int voltage)
{
	enum ec_error_list rv;
	int volt_bits;

	volt_bits = SM5803_VOLTAGE_TO_REG(voltage);

	/*
	 * Note: Set both voltages on both chargers.  Vbat will only be used on
	 * primary, which enables charging.
	 */
	rv = chg_write8(chgnum, SM5803_REG_VSYS_PREREG_MSB, (volt_bits >> 3));
	rv |= chg_write8(chgnum, SM5803_REG_VSYS_PREREG_LSB, (volt_bits & 0x7));
	rv |= chg_write8(chgnum, SM5803_REG_VBAT_FAST_MSB, (volt_bits >> 3));
	rv |= chg_write8(chgnum, SM5803_REG_VBAT_FAST_LSB, (volt_bits & 0x7));

	return rv;
}

static enum ec_error_list sm5803_get_vbus_voltage(int chgnum, int port,
						   int *voltage)
{
	enum ec_error_list rv;
	int reg;
	int volt_bits;

	rv = meas_read8(chgnum, SM5803_REG_VBUS_MEAS_MSB, &reg);
	if (rv)
		return rv;

	volt_bits = reg << 2;

	rv = meas_read8(chgnum, SM5803_REG_VBUS_MEAS_LSB, &reg);

	volt_bits |= reg & SM5803_VBUS_MEAS_LSB;

	/* Vbus ADC is in 23.4 mV steps */
	*voltage = (int)((float)volt_bits * 23.4f);
	return rv;
}

static enum ec_error_list sm5803_set_input_current(int chgnum,
						   int input_current)
{
	int reg;

	reg = SM5803_CURRENT_TO_REG(input_current) & SM5803_CHG_ILIM_RAW;

	return chg_write8(chgnum, SM5803_REG_CHG_ILIM, reg);
}

static enum ec_error_list sm5803_get_input_current(int chgnum,
						   int *input_current)
{
	enum ec_error_list rv;
	int reg;

	rv = chg_read8(chgnum, SM5803_REG_CHG_ILIM, &reg);
	if (rv)
		return rv;

	*input_current = SM5803_REG_TO_CURRENT(reg & SM5803_CHG_ILIM_RAW);
	return EC_SUCCESS;
}

static enum ec_error_list sm5803_get_option(int chgnum, int *option)
{
	enum ec_error_list rv;
	uint32_t control;
	int reg;

	rv = chg_read8(chgnum, SM5803_REG_FLOW1, &reg);
	control = reg;

	rv |= chg_read8(chgnum, SM5803_REG_FLOW2, &reg);
	control |= reg << 8;

	rv |= chg_read8(chgnum, SM5803_REG_FLOW3, &reg);
	control |= reg << 16;

	return rv;
}

static enum ec_error_list sm5803_set_option(int chgnum, int option)
{
	enum ec_error_list rv;
	int reg;

	reg = option & 0xFF;
	rv = chg_write8(chgnum, SM5803_REG_FLOW1, reg);
	if (rv)
		return rv;

	reg = (option >> 8) & 0xFF;
	rv = chg_write8(chgnum, SM5803_REG_FLOW2, reg);
	if (rv)
		return rv;

	reg = (option >> 16) & 0xFF;
	rv = chg_write8(chgnum, SM5803_REG_FLOW3, reg);

	return rv;
}

const struct charger_drv sm5803_drv = {
	.init = &sm5803_init,
	.post_init = &sm5803_post_init,
	.get_info = &sm5803_get_info,
	.get_status = &sm5803_get_status,
	.set_mode = &sm5803_set_mode,
	.get_current = &sm5803_get_current,
	.set_current = &sm5803_set_current,
	.get_voltage = &sm5803_get_voltage,
	.set_voltage = &sm5803_set_voltage,
	.get_vbus_voltage = &sm5803_get_vbus_voltage,
	.set_input_current = &sm5803_set_input_current,
	.get_input_current = &sm5803_get_input_current,
	.get_option = &sm5803_get_option,
	.set_option = &sm5803_set_option,
};
