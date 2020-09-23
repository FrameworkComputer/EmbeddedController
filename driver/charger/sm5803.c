/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Silicon Mitus SM5803 Buck-Boost Charger
 */
#include "atomic.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "sm5803.h"
#include "throttle_ap.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"
#include "watchdog.h"

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "SM5803 is a NVDC charger, please enable CONFIG_CHARGER_NARROW_VDC."
#endif

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define UNKNOWN_DEV_ID -1
static int dev_id = UNKNOWN_DEV_ID;

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

static uint32_t irq_pending; /* Bitmask of chips with interrupts pending */

static struct mutex flow1_access_lock[CHARGER_NUM];
static struct mutex flow2_access_lock[CHARGER_NUM];

static int charger_vbus[CHARGER_NUM];

static int sm5803_is_sourcing_otg_power(int chgnum, int port);
static enum ec_error_list sm5803_get_dev_id(int chgnum, int *id);

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

static inline enum ec_error_list test_write8(int chgnum, int offset, int value)
{
	return i2c_write8(chg_chips[chgnum].i2c_port,
			 SM5803_ADDR_TEST_FLAGS,
			 offset, value);
}

static inline enum ec_error_list test_update8(int chgnum, const int offset,
					      const uint8_t mask,
					const enum mask_update_action action)
{
	return i2c_update8(chg_chips[chgnum].i2c_port,
			   SM5803_ADDR_TEST_FLAGS, offset, mask, action);
}

static enum ec_error_list sm5803_flow1_update(int chgnum, const uint8_t mask,
					const enum mask_update_action action)
{
	int reg, rv, dev_id;

	/*
	 * On Si rev 3, confirm that init value in 0x5C is intact before
	 * enabling charging.
	 */
	rv = sm5803_get_dev_id(chgnum, &dev_id);
	if (rv)
		return rv;

	if (dev_id == 0x03) {
		rv = chg_read8(chgnum, 0x5C, &reg);
		if (rv) {
			CPRINTS("%s %d: Failed 0x5C read",
				CHARGER_NAME, chgnum);
			return rv;
		}

		if (reg != 0x7A) {
			CPRINTS("%s %d: Unexpected 0x5C reg: 0x%02x. File bug",
				CHARGER_NAME, chgnum, reg);

			/* Fix it before enabling charging */
			rv = chg_write8(chgnum, 0x5C, 0x7A);
		}
	}

	/* Safety checks done, onto the actual register update */
	mutex_lock(&flow1_access_lock[chgnum]);

	rv = i2c_update8(chg_chips[chgnum].i2c_port,
			 chg_chips[chgnum].i2c_addr_flags,
			 SM5803_REG_FLOW1,
			 mask, action);

	mutex_unlock(&flow1_access_lock[chgnum]);

	return rv;
}

static enum ec_error_list sm5803_flow2_update(int chgnum, const uint8_t mask,
					const enum mask_update_action action)
{
	int rv;

	mutex_lock(&flow2_access_lock[chgnum]);

	rv = i2c_update8(chg_chips[chgnum].i2c_port,
			 chg_chips[chgnum].i2c_addr_flags,
			 SM5803_REG_FLOW2,
			 mask, action);

	mutex_unlock(&flow2_access_lock[chgnum]);

	return rv;
}

int sm5803_is_vbus_present(int chgnum)
{
	return charger_vbus[chgnum];
}

enum ec_error_list sm5803_configure_gpio0(int chgnum,
					  enum sm5803_gpio0_modes mode, int od)
{
	enum ec_error_list rv;
	int reg;

	rv = main_read8(chgnum, SM5803_REG_GPIO0_CTRL, &reg);
	if (rv)
		return rv;

	reg &= ~SM5803_GPIO0_MODE_MASK;
	reg |= mode << 1;

	if (od)
		reg |= SM5803_GPIO0_OPEN_DRAIN_EN;
	else
		reg &= ~SM5803_GPIO0_OPEN_DRAIN_EN;

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

enum ec_error_list sm5803_configure_chg_det_od(int chgnum, int enable)
{
	enum ec_error_list rv;
	int reg;

	rv = main_read8(chgnum, SM5803_REG_GPIO0_CTRL, &reg);
	if (rv)
		return rv;

	if (enable)
		reg |= SM5803_CHG_DET_OPEN_DRAIN_EN;
	else
		reg &= ~SM5803_CHG_DET_OPEN_DRAIN_EN;

	rv = main_write8(chgnum, SM5803_REG_GPIO0_CTRL, reg);
	return rv;
}

enum ec_error_list sm5803_get_chg_det(int chgnum, int *chg_det)
{
	enum ec_error_list rv;
	int reg;

	rv = main_read8(chgnum, SM5803_REG_STATUS1, &reg);
	if (rv)
		return rv;

	*chg_det = (reg & SM5803_STATUS1_CHG_DET) != 0;

	return EC_SUCCESS;
}

enum ec_error_list sm5803_set_vbus_disch(int chgnum, int enable)
{
	enum ec_error_list rv;
	int reg;

	rv = main_read8(chgnum, SM5803_REG_PORTS_CTRL, &reg);
	if (rv)
		return rv;

	if (enable)
		reg |= SM5803_PORTS_VBUS_DISCH;
	else
		reg &= ~SM5803_PORTS_VBUS_DISCH;

	rv = main_write8(chgnum, SM5803_REG_PORTS_CTRL, reg);
	return rv;
}

enum ec_error_list sm5803_vbus_sink_enable(int chgnum, int enable)
{
	enum ec_error_list rv;
	int regval;

	rv = sm5803_get_dev_id(chgnum, &dev_id);
	if (rv)
		return rv;

	if (enable) {
		if (chgnum == CHARGER_PRIMARY) {
			/* Magic for new silicon */
			if (dev_id >= 3) {
				rv |= main_write8(chgnum, 0x1F, 0x1);
				rv |= test_write8(chgnum, 0x44, 0x2);
				rv |= main_write8(chgnum, 0x1F, 0);
			}
			rv = sm5803_flow2_update(chgnum,
						 SM5803_FLOW2_AUTO_ENABLED,
						 MASK_SET);
		} else {
			if (dev_id >= 3) {
				/* Touch of magic on the primary charger */
				rv |= main_write8(CHARGER_PRIMARY, 0x1F, 0x1);
				rv |= test_write8(CHARGER_PRIMARY, 0x44, 0x20);
				rv |= main_write8(CHARGER_PRIMARY, 0x1F, 0x0);

				/*
				 * Disable linear, pre-charge, and linear fast
				 * charge for primary charger.
				 */
				rv = chg_read8(CHARGER_PRIMARY,
					       SM5803_REG_FLOW3, &regval);
				regval &= ~(BIT(6) | BIT(5) | BIT(4));

				rv |= chg_write8(CHARGER_PRIMARY,
						 SM5803_REG_FLOW3, regval);
			}
		}

		/* Last but not least, enable sinking */
		rv |= sm5803_flow1_update(chgnum, CHARGER_MODE_SINK, MASK_SET);
	} else {
		if (chgnum == CHARGER_PRIMARY)
			rv |= sm5803_flow2_update(chgnum,
						  SM5803_FLOW2_AUTO_ENABLED,
						  MASK_CLR);

		if (chgnum == CHARGER_SECONDARY) {
			rv |= sm5803_flow1_update(CHARGER_PRIMARY,
						  SM5803_FLOW1_LINEAR_CHARGE_EN,
						  MASK_CLR);

			rv = chg_read8(CHARGER_PRIMARY, SM5803_REG_FLOW3,
				       &regval);
			regval &= ~(BIT(6) | BIT(5) | BIT(4));
			rv |= chg_write8(CHARGER_PRIMARY, SM5803_REG_FLOW3,
					 regval);
		}


		/* Disable sink mode, unless currently sourcing out */
		if (!sm5803_is_sourcing_otg_power(chgnum, chgnum))
			rv |= sm5803_flow1_update(chgnum, CHARGER_MODE_SINK,
						  MASK_CLR);
	}

	return rv;

}

static void sm5803_init(int chgnum)
{
	enum ec_error_list rv;
	int reg;
	int vbus_mv;
	const struct battery_info *batt_info;
	int pre_term;
	int cells;

	/*
	 * If a charger is not currently present, disable switching per OCPC
	 * requirements
	 */
	rv = charger_get_vbus_voltage(chgnum, &vbus_mv);
	if (rv == EC_SUCCESS) {
		if (vbus_mv < 4000) {
			/*
			 * No charger connected, disable CHG_EN
			 * (note other bits default to 0)
			 */
			rv = chg_write8(chgnum, SM5803_REG_FLOW1, 0);
		} else if (!sm5803_is_sourcing_otg_power(chgnum, chgnum)) {
			charger_vbus[chgnum] = 1;
		}
	} else {
		CPRINTS("%s %d: Failed to read VBUS voltage during init",
			CHARGER_NAME, chgnum);
		return;
	}

	rv |= charger_device_id(&reg);
	if (reg == 0x02) {
		/* --- Special register init ---
		 * For early silicon (ID 2) with 3S batteries
		 */
		rv |= main_write8(chgnum, 0x20, 0x08);
		rv |= main_write8(chgnum, 0x30, 0xC0);
		rv |= main_write8(chgnum, 0x80, 0x01);

		rv |= meas_write8(chgnum, 0x08, 0xC2);

		rv |= chg_write8(chgnum, 0x1D, 0x40);
		rv |= chg_write8(chgnum, 0x1F, 0x09);

		rv |= chg_write8(chgnum, 0x22, 0xB3);
		rv |= chg_write8(chgnum, 0x23, 0x81);
		rv |= chg_write8(chgnum, 0x28, 0xB7);

		rv |= chg_write8(chgnum, 0x4A, 0x82);
		rv |= chg_write8(chgnum, 0x4B, 0xA3);
		rv |= chg_write8(chgnum, 0x4C, 0xA8);
		rv |= chg_write8(chgnum, 0x4D, 0xCA);
		rv |= chg_write8(chgnum, 0x4E, 0x07);
		rv |= chg_write8(chgnum, 0x4F, 0xFF);

		rv |= chg_write8(chgnum, 0x50, 0x98);
		rv |= chg_write8(chgnum, 0x51, 0x00);
		rv |= chg_write8(chgnum, 0x52, 0x77);
		rv |= chg_write8(chgnum, 0x53, 0xD2);
		rv |= chg_write8(chgnum, 0x54, 0x02);
		rv |= chg_write8(chgnum, 0x55, 0xD1);
		rv |= chg_write8(chgnum, 0x56, 0x7F);
		rv |= chg_write8(chgnum, 0x57, 0x02);
		rv |= chg_write8(chgnum, 0x58, 0xD1);
		rv |= chg_write8(chgnum, 0x59, 0x7F);
		rv |= chg_write8(chgnum, 0x5A, 0x13);
		rv |= chg_write8(chgnum, 0x5B, 0x50);
		rv |= chg_write8(chgnum, 0x5C, 0x5B);
		rv |= chg_write8(chgnum, 0x5D, 0xB0);
		rv |= chg_write8(chgnum, 0x5E, 0x3C);
		rv |= chg_write8(chgnum, 0x5F, 0x3C);

		rv |= chg_write8(chgnum, 0x60, 0x44);
		rv |= chg_write8(chgnum, 0x61, 0x20);
		rv |= chg_write8(chgnum, 0x65, 0x35);
		rv |= chg_write8(chgnum, 0x66, 0x29);
		rv |= chg_write8(chgnum, 0x67, 0x64);
		rv |= chg_write8(chgnum, 0x68, 0x88);
		rv |= chg_write8(chgnum, 0x69, 0xC7);

		/* Inits to access page 0x37 and enable trickle charging */
		rv |= main_write8(chgnum, 0x1F, 0x01);
		rv |= test_update8(chgnum, 0x8E, BIT(5), MASK_SET);
		rv |= main_write8(chgnum, 0x1F, 0x00);
	} else if (reg == 0x03) {
		uint32_t platform_id;

		rv = main_read8(chgnum, SM5803_REG_PLATFORM, &platform_id);
		if (rv) {
			CPRINTS("%s %d: Failed to read platform during init",
							CHARGER_NAME, chgnum);
			return;
		}
		platform_id &= SM5803_PLATFORM_ID;

		if (platform_id >= 0x0E && platform_id <= 0x16) {
			/* 3S Battery inits */
			rv |= main_write8(chgnum, 0x30, 0xC0);
			rv |= main_write8(chgnum, 0x80, 0x01);
			rv |= main_write8(chgnum, 0x1A, 0x08);

			rv |= meas_write8(chgnum, 0x08, 0xC2);

			rv |= chg_write8(chgnum, 0x1D, 0x40);

			rv |= chg_write8(chgnum, 0x22, 0xB3);

			rv |= chg_write8(chgnum, 0x3E, 0x3C);

			rv |= chg_write8(chgnum, 0x4B, 0xA6);
			rv |= chg_write8(chgnum, 0x4F, 0xBF);

			rv |= chg_write8(chgnum, 0x52, 0x77);
			rv |= chg_write8(chgnum, 0x53, 0xD2);
			rv |= chg_write8(chgnum, 0x54, 0x02);
			rv |= chg_write8(chgnum, 0x55, 0xD1);
			rv |= chg_write8(chgnum, 0x56, 0x7F);
			rv |= chg_write8(chgnum, 0x57, 0x01);
			rv |= chg_write8(chgnum, 0x58, 0x50);
			rv |= chg_write8(chgnum, 0x59, 0x7F);
			rv |= chg_write8(chgnum, 0x5A, 0x13);
			rv |= chg_write8(chgnum, 0x5B, 0x50);
			rv |= chg_write8(chgnum, 0x5D, 0xB0);

			rv |= chg_write8(chgnum, 0x60, 0x44);
			rv |= chg_write8(chgnum, 0x65, 0x35);
			rv |= chg_write8(chgnum, 0x66, 0x29);

			rv |= chg_write8(chgnum, 0x7D, 0x67);
			rv |= chg_write8(chgnum, 0x7E, 0x04);

			rv |= chg_write8(chgnum, 0x33, 0x3C);

			rv |= chg_write8(chgnum, 0x5C, 0x7A);
		} else if (platform_id >= 0x06 && platform_id <= 0x0D) {
			/* 2S Battery inits */
			rv |= main_write8(chgnum, 0x30, 0xC0);
			rv |= main_write8(chgnum, 0x80, 0x01);
			rv |= main_write8(chgnum, 0x1A, 0x08);

			rv |= meas_write8(chgnum, 0x08, 0xC2);

			rv |= chg_write8(chgnum, 0x1D, 0x40);

			rv |= chg_write8(chgnum, 0x22, 0xB3);

			rv |= chg_write8(chgnum, 0x3E, 0x3C);

			rv |= chg_write8(chgnum, 0x4F, 0xBF);

			rv |= chg_write8(chgnum, 0x52, 0x77);
			rv |= chg_write8(chgnum, 0x53, 0xD2);
			rv |= chg_write8(chgnum, 0x54, 0x02);
			rv |= chg_write8(chgnum, 0x55, 0xD1);
			rv |= chg_write8(chgnum, 0x56, 0x7F);
			rv |= chg_write8(chgnum, 0x57, 0x01);
			rv |= chg_write8(chgnum, 0x58, 0x50);
			rv |= chg_write8(chgnum, 0x59, 0x7F);
			rv |= chg_write8(chgnum, 0x5A, 0x13);
			rv |= chg_write8(chgnum, 0x5B, 0x52);
			rv |= chg_write8(chgnum, 0x5D, 0xD0);

			rv |= chg_write8(chgnum, 0x60, 0x44);
			rv |= chg_write8(chgnum, 0x65, 0x35);
			rv |= chg_write8(chgnum, 0x66, 0x29);

			rv |= chg_write8(chgnum, 0x7D, 0x97);
			rv |= chg_write8(chgnum, 0x7E, 0x07);

			rv |= chg_write8(chgnum, 0x33, 0x3C);

			rv |= chg_write8(chgnum, 0x5C, 0x7A);
		}

		rv |= chg_write8(chgnum, 0x73, 0x22);
		rv |= chg_write8(chgnum, 0x50, 0x88);
		rv |= chg_read8(chgnum, 0x34, &reg);
		reg |= BIT(7);
		rv |= chg_write8(chgnum, 0x34, reg);
		rv |= main_write8(chgnum, 0x1F, 0x1);
		rv |= test_write8(chgnum, 0x43, 0x10);
		rv |= test_write8(chgnum, 0x47, 0x10);
		rv |= test_write8(chgnum, 0x48, 0x04);
		rv |= main_write8(chgnum, 0x1F, 0x0);
	}

	/* Enable LDO bits */
	rv |= main_read8(chgnum, SM5803_REG_REFERENCE, &reg);
	reg &= ~(BIT(0) | BIT(1));
	rv |= main_write8(chgnum, SM5803_REG_REFERENCE, reg);

	/* Set a higher clock speed in case it was lowered for z-state */
	rv |= main_read8(chgnum, SM5803_REG_CLOCK_SEL, &reg);
	reg &= ~SM5803_CLOCK_SEL_LOW;
	rv |= main_write8(chgnum, SM5803_REG_CLOCK_SEL, reg);

	/* Turn on GPADCs to default */
	rv |= meas_write8(chgnum, SM5803_REG_GPADC_CONFIG1, 0xF3);

	/* Enable Psys DAC */
	rv |= meas_read8(chgnum, SM5803_REG_PSYS1, &reg);
	reg |= SM5803_PSYS1_DAC_EN;
	rv |= meas_write8(chgnum, SM5803_REG_PSYS1, reg);

	/* Enable ADC sigma delta */
	rv |= chg_read8(chgnum, SM5803_REG_CC_CONFIG1, &reg);
	reg |= SM5803_CC_CONFIG1_SD_PWRUP;
	rv |= chg_write8(chgnum, SM5803_REG_CC_CONFIG1, reg);

	/* Enable PROCHOT comparators except Ibus */
	rv |= chg_read8(chgnum, SM5803_REG_PHOT1, &reg);
	reg |= SM5803_PHOT1_COMPARATOR_EN;
	reg &= ~SM5803_PHOT1_IBUS_PHOT_COMP_EN;
	rv |= chg_write8(chgnum, SM5803_REG_PHOT1, reg);

	if (chgnum != CHARGER_PRIMARY) {
		/*
		 * Enable the IBAT_CHG adc in order to calculate
		 * system resistance.
		 */
		rv |= meas_read8(chgnum, SM5803_REG_GPADC_CONFIG1, &reg);
		reg |= SM5803_GPADCC1_IBAT_CHG_EN;
		rv |= meas_write8(chgnum, SM5803_REG_GPADC_CONFIG1, reg);
	}

	/* Set default input current */
	reg = SM5803_CURRENT_TO_REG(CONFIG_CHARGER_INPUT_CURRENT)
		& SM5803_CHG_ILIM_RAW;
	rv |= chg_write8(chgnum, SM5803_REG_CHG_ILIM, reg);

	/* Configure charger insertion interrupts */
	rv |= main_write8(chgnum, SM5803_REG_INT1_EN, SM5803_INT1_CHG);
	/* Enable end of charge interrupts for logging */
	rv |= main_write8(chgnum, SM5803_REG_INT4_EN, SM5803_INT4_CHG_FAIL |
						      SM5803_INT4_CHG_DONE);

	/* Set TINT interrupts for 360 K and 330 K */
	rv |= meas_write8(chgnum, SM5803_REG_TINT_HIGH_TH,
						SM5803_TINT_HIGH_LEVEL);
	rv |= meas_write8(chgnum, SM5803_REG_TINT_LOW_TH,
						SM5803_TINT_LOW_LEVEL);

	/* Configure TINT interrupts to fire after thresholds are set */
	rv |= main_write8(chgnum, SM5803_REG_INT2_EN, SM5803_INT2_TINT);

	/*
	 * Configure CHG_ENABLE to only be set through I2C by setting
	 * HOST_MODE_EN bit (all other register bits are 0 by default)
	 */
	rv |= chg_write8(chgnum, SM5803_REG_FLOW2, SM5803_FLOW2_HOST_MODE_EN);

	if (chgnum == CHARGER_PRIMARY) {
		int ibat_eoc_ma;

		/* Set end of fast charge threshold */
		batt_info = battery_get_info();
		ibat_eoc_ma = batt_info->precharge_current - 50;
		ibat_eoc_ma /= 100;
		ibat_eoc_ma = CLAMP(ibat_eoc_ma, 0, SM5803_CONF5_IBAT_EOC_TH);
		rv |= chg_read8(chgnum, SM5803_REG_FAST_CONF5, &reg);
		reg &= ~SM5803_CONF5_IBAT_EOC_TH;
		reg |= ibat_eoc_ma;
		rv |= chg_write8(CHARGER_PRIMARY, SM5803_REG_FAST_CONF5, reg);

		/* Setup the proper precharge thresholds. */
		cells = batt_info->voltage_max / 4;
		pre_term = batt_info->voltage_min / cells;
		pre_term /= 100; /* Convert to decivolts. */
		pre_term = CLAMP(pre_term, SM5803_VBAT_PRE_TERM_MIN_DV,
				 SM5803_VBAT_PRE_TERM_MAX_DV);
		pre_term -= SM5803_VBAT_PRE_TERM_MIN_DV; /* Convert to regval */

		rv |= chg_read8(chgnum, SM5803_REG_PRE_FAST_CONF_REG1, &reg);
		reg &= ~SM5803_VBAT_PRE_TERM;
		reg |= pre_term << SM5803_VBAT_PRE_TERM_SHIFT;
		rv |= chg_write8(chgnum, SM5803_REG_PRE_FAST_CONF_REG1, reg);

		/*
		 * Set up precharge current
		 * Note it is preferred to under-shoot the precharge current
		 * requested. Upper bits of this register are read/write 1 to
		 * clear
		 */
		reg = SM5803_CURRENT_TO_REG(batt_info->precharge_current);
		reg = MIN(reg, SM5803_PRECHG_ICHG_PRE_SET);
		rv |= chg_write8(chgnum, SM5803_REG_PRECHG, reg);

		/*
		 * Set up BFET alerts
		 *
		 * We'll set the soft limit at 1.5W and the hard limit at 6W.
		 *
		 * The register is 29.2 mW per bit.
		 */
		reg = (1500 * 10) / 292;
		rv |= meas_write8(chgnum, SM5803_REG_BFET_PWR_MAX_TH, reg);
		reg = (6000 * 10) / 292;
		rv |= meas_write8(chgnum, SM5803_REG_BFET_PWR_HWSAFE_MAX_TH,
				 reg);
		rv |= main_read8(chgnum, SM5803_REG_INT3_EN, &reg);
		reg |= SM5803_INT3_BFET_PWR_LIMIT |
		       SM5803_INT3_BFET_PWR_HWSAFE_LIMIT;
		rv |= main_write8(chgnum, SM5803_REG_INT3_EN, reg);

		rv |= chg_read8(chgnum, SM5803_REG_FLOW3, &reg);
		reg &= ~SM5803_FLOW3_SWITCH_BCK_BST;
		rv |= chg_write8(chgnum, SM5803_REG_FLOW3, reg);

		rv |= chg_read8(chgnum, SM5803_REG_SWITCHER_CONF, &reg);
		reg |= SM5803_SW_BCK_BST_CONF_AUTO;
		rv |= chg_write8(chgnum, SM5803_REG_SWITCHER_CONF, reg);
	}

	if (rv)
		CPRINTS("%s %d: Failed initialization", CHARGER_NAME, chgnum);
}

static enum ec_error_list sm5803_post_init(int chgnum)
{
	/* Nothing to do, charger is always powered */
	return EC_SUCCESS;
}

void sm5803_hibernate(int chgnum)
{
	enum ec_error_list rv;
	int reg;

	rv = main_read8(chgnum, SM5803_REG_REFERENCE, &reg);
	if (rv) {
		CPRINTS("%s %d: Failed to read REFERENCE reg", CHARGER_NAME,
			chgnum);
		return;
	}

	/* Disable LDO bits - note the primary LDO should not be disabled */
	if (chgnum != CHARGER_PRIMARY) {
		reg |= (BIT(0) | BIT(1));
		rv |= main_write8(chgnum, SM5803_REG_REFERENCE, reg);
	}

	/* Slow the clock speed */
	rv |= main_read8(chgnum, SM5803_REG_CLOCK_SEL, &reg);
	reg |= SM5803_CLOCK_SEL_LOW;
	rv |= main_write8(chgnum, SM5803_REG_CLOCK_SEL, reg);

	/* Turn off GPADCs */
	rv |= meas_write8(chgnum, SM5803_REG_GPADC_CONFIG1, 0);
	rv |= meas_write8(chgnum, SM5803_REG_GPADC_CONFIG2, 0);

	/* Disable Psys DAC */
	rv |= meas_read8(chgnum, SM5803_REG_PSYS1, &reg);
	reg &= ~SM5803_PSYS1_DAC_EN;
	rv |= meas_write8(chgnum, SM5803_REG_PSYS1, reg);

	/* Disable ADC sigma delta */
	rv |= chg_read8(chgnum, SM5803_REG_CC_CONFIG1, &reg);
	reg &= ~SM5803_CC_CONFIG1_SD_PWRUP;
	rv |= chg_write8(chgnum, SM5803_REG_CC_CONFIG1, reg);

	/* Disable PROCHOT comparators */
	rv |= chg_read8(chgnum, SM5803_REG_PHOT1, &reg);
	reg &= ~SM5803_PHOT1_COMPARATOR_EN;
	rv |= chg_write8(chgnum, SM5803_REG_PHOT1, reg);

	if (rv)
		CPRINTS("%s %d: Failed to set hibernate", CHARGER_NAME, chgnum);
}

static void sm5803_disable_runtime_low_power_mode(void)
{
	enum ec_error_list rv;
	int reg;
	int chgnum = TASK_ID_TO_PD_PORT(task_get_current());

	CPRINTS("%s %d: disable runtime low power mode", CHARGER_NAME, chgnum);
	rv = main_read8(chgnum, SM5803_REG_REFERENCE, &reg);
	if (rv) {
		CPRINTS("%s %d: Failed to read REFERENCE reg", CHARGER_NAME,
			chgnum);
		return;
	}
	/* Set a higher clock speed */
	rv |= main_read8(chgnum, SM5803_REG_CLOCK_SEL, &reg);
	reg &= ~SM5803_CLOCK_SEL_LOW;
	rv |= main_write8(chgnum, SM5803_REG_CLOCK_SEL, reg);

	/* Enable ADC sigma delta */
	rv |= chg_read8(chgnum, SM5803_REG_CC_CONFIG1, &reg);
	reg |= SM5803_CC_CONFIG1_SD_PWRUP;
	rv |= chg_write8(chgnum, SM5803_REG_CC_CONFIG1, reg);

	if (rv)
		CPRINTS("%s %d: Failed to set in disable runtime LPM",
			CHARGER_NAME, chgnum);
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT,
		sm5803_disable_runtime_low_power_mode,
		HOOK_PRIO_FIRST);

static void sm5803_enable_runtime_low_power_mode(void)
{
	enum ec_error_list rv;
	int reg;
	int chgnum = TASK_ID_TO_PD_PORT(task_get_current());

	CPRINTS("%s %d: enable runtime low power mode", CHARGER_NAME, chgnum);
	rv = main_read8(chgnum, SM5803_REG_REFERENCE, &reg);
	if (rv) {
		CPRINTS("%s %d: Failed to read REFERENCE reg", CHARGER_NAME,
			chgnum);
		return;
	}
	/* Slow the clock speed */
	rv |= main_read8(chgnum, SM5803_REG_CLOCK_SEL, &reg);
	reg |= SM5803_CLOCK_SEL_LOW;
	rv |= main_write8(chgnum, SM5803_REG_CLOCK_SEL, reg);

	/* Disable ADC sigma delta */
	rv |= chg_read8(chgnum, SM5803_REG_CC_CONFIG1, &reg);
	reg &= ~SM5803_CC_CONFIG1_SD_PWRUP;
	rv |= chg_write8(chgnum, SM5803_REG_CC_CONFIG1, reg);

	/* If the system is off, all PROCHOT comparators may be turned off */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF |
			     CHIPSET_STATE_ANY_SUSPEND)) {
		rv |= chg_read8(chgnum, SM5803_REG_PHOT1, &reg);
		reg &= ~SM5803_PHOT1_COMPARATOR_EN;
		rv |= chg_write8(chgnum, SM5803_REG_PHOT1, reg);
	}

	if (rv)
		CPRINTS("%s %d: Failed to set in enable runtime LPM",
			CHARGER_NAME, chgnum);
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT,
		sm5803_enable_runtime_low_power_mode,
		HOOK_PRIO_LAST);

void sm5803_disable_low_power_mode(int chgnum)
{
	enum ec_error_list rv;
	int reg;

	CPRINTS("%s %d: disable low power mode", CHARGER_NAME, chgnum);
	rv = main_read8(chgnum, SM5803_REG_REFERENCE, &reg);
	if (rv) {
		CPRINTS("%s %d: Failed to read REFERENCE reg", CHARGER_NAME,
			chgnum);
		return;
	}
	/* Enable Psys DAC */
	rv |= meas_read8(chgnum, SM5803_REG_PSYS1, &reg);
	reg |= SM5803_PSYS1_DAC_EN;
	rv |= meas_write8(chgnum, SM5803_REG_PSYS1, reg);

	/* Enable PROCHOT comparators except Ibus */
	rv |= chg_read8(chgnum, SM5803_REG_PHOT1, &reg);
	reg |= SM5803_PHOT1_COMPARATOR_EN;
	reg &= ~SM5803_PHOT1_IBUS_PHOT_COMP_EN;
	rv |= chg_write8(chgnum, SM5803_REG_PHOT1, reg);

	if (rv)
		CPRINTS("%s %d: Failed to set in disable low power mode",
			CHARGER_NAME, chgnum);
}

void sm5803_enable_low_power_mode(int chgnum)
{
	enum ec_error_list rv;
	int reg;

	CPRINTS("%s %d: enable low power mode", CHARGER_NAME, chgnum);
	rv = main_read8(chgnum, SM5803_REG_REFERENCE, &reg);
	if (rv) {
		CPRINTS("%s %d: Failed to read REFERENCE reg", CHARGER_NAME,
			chgnum);
		return;
	}
	/* Disable Psys DAC */
	rv |= meas_read8(chgnum, SM5803_REG_PSYS1, &reg);
	reg &= ~SM5803_PSYS1_DAC_EN;
	rv |= meas_write8(chgnum, SM5803_REG_PSYS1, reg);

	/*
	 * Disable all PROCHOT comparators only if port is inactive.  Vbus
	 * sourcing requires that the Vbus comparator be enabled, and it
	 * cannot be enabled from HOOK_USB_PD_CONNECT since that is
	 * called after Vbus has turned on.
	 */
	rv |= chg_read8(chgnum, SM5803_REG_PHOT1, &reg);
	reg &= ~SM5803_PHOT1_COMPARATOR_EN;
	if (pd_is_connected(chgnum))
		reg |= SM5803_PHOT1_VBUS_MON_EN;
	rv |= chg_write8(chgnum, SM5803_REG_PHOT1, reg);


	if (rv)
		CPRINTS("%s %d: Failed to set in enable low power mode",
			CHARGER_NAME, chgnum);
}

/*
 * Process interrupt registers and report any Vbus changes.  Alert the AP if the
 * charger has become too hot.
 */
void sm5803_handle_interrupt(int chgnum)
{
	enum ec_error_list rv;
	int int_reg, meas_reg;
	static bool throttled;
	struct batt_params bp;
	int act_chg, val;

	/* Note: Interrupt registers are clear on read */
	rv = main_read8(chgnum, SM5803_REG_INT1_REQ, &int_reg);
	if (rv) {
		CPRINTS("%s %d: Failed read int1 register", CHARGER_NAME,
			chgnum);
		return;
	}

	if (int_reg & SM5803_INT1_CHG) {
		rv = main_read8(chgnum, SM5803_REG_STATUS1, &meas_reg);
		if (!(meas_reg & SM5803_STATUS1_CHG_DET)) {
			charger_vbus[chgnum] = 0;
			if (IS_ENABLED(CONFIG_USB_CHARGER))
				usb_charger_vbus_change(chgnum, 0);
			board_vbus_present_change();
		} else {
			charger_vbus[chgnum] = 1;
			if (IS_ENABLED(CONFIG_USB_CHARGER))
				usb_charger_vbus_change(chgnum, 1);
			board_vbus_present_change();
		}
	}

	rv = main_read8(chgnum, SM5803_REG_INT2_REQ, &int_reg);
	if (rv) {
		CPRINTS("%s %d: Failed read int2 register", CHARGER_NAME,
			chgnum);
		return;
	}

	if (int_reg & SM5803_INT2_TINT) {
		/*
		 * Ignore any interrupts from the low threshold when not
		 * throttled in order to prevent console spam when the
		 * temperature is holding near the threshold.
		 */
		rv = meas_read8(chgnum, SM5803_REG_TINT_MEAS_MSB, &meas_reg);
		if ((meas_reg <= SM5803_TINT_LOW_LEVEL) && throttled) {
			throttled = false;
			throttle_ap(THROTTLE_OFF, THROTTLE_HARD,
							THROTTLE_SRC_THERMAL);
		} else if (meas_reg >= SM5803_TINT_HIGH_LEVEL) {
			throttled = true;
			throttle_ap(THROTTLE_ON, THROTTLE_HARD,
							THROTTLE_SRC_THERMAL);
		}
		/*
		 * If the interrupt came in and we're not currently throttling
		 * or the level is below the upper threshold, it can likely be
		 * ignored.
		 */
	}

	/* TODO(b/159376384): Take action on fatal BFET power alert. */
	rv = main_read8(chgnum, SM5803_REG_INT3_REQ, &int_reg);
	if (rv) {
		CPRINTS("%s %d: Failed to read int3 register", CHARGER_NAME,
			chgnum);
		return;
	}

	if ((int_reg & SM5803_INT3_BFET_PWR_LIMIT) ||
	    (int_reg & SM5803_INT3_BFET_PWR_HWSAFE_LIMIT)) {
		battery_get_params(&bp);
		act_chg = charge_manager_get_active_charge_port();
		CPRINTS("%s BFET power limit reached! (%s)", CHARGER_NAME,
			(int_reg & SM5803_INT3_BFET_PWR_LIMIT) ? "warn" :
			"FATAL");
		CPRINTS("\tVbat: %dmV", bp.voltage);
		CPRINTS("\tIbat: %dmA", bp.current);
		charger_get_voltage(act_chg, &val);
		CPRINTS("\tVsys(aux): %dmV", val);
		charger_get_current(act_chg, &val);
		CPRINTS("\tIsys: %dmA", val);
		cflush();
	}

	rv = main_read8(chgnum, SM5803_REG_INT4_REQ, &int_reg);
	if (rv) {
		CPRINTS("%s %d: Failed to read int4 register", CHARGER_NAME,
			chgnum);
		return;
	}

	if (int_reg & SM5803_INT4_CHG_FAIL)
		CPRINTS("%s %d: CHG_FAIL_INT fired!!!", CHARGER_NAME, chgnum);

	if (int_reg & SM5803_INT4_CHG_DONE)
		CPRINTS("%s %d: CHG_DONE_INT fired!!!", CHARGER_NAME, chgnum);
}

static void sm5803_irq_deferred(void)
{
	int i;
	uint32_t pending = deprecated_atomic_read_clear(&irq_pending);

	for (i = 0; i < CHARGER_NUM; i++)
		if (BIT(i) & pending)
			sm5803_handle_interrupt(i);
}
DECLARE_DEFERRED(sm5803_irq_deferred);

void sm5803_interrupt(int chgnum)
{
	deprecated_atomic_or(&irq_pending, BIT(chgnum));
	hook_call_deferred(&sm5803_irq_deferred_data, 0);
}

static enum ec_error_list sm5803_get_dev_id(int chgnum, int *id)
{
	int rv = EC_SUCCESS;

	if (dev_id == UNKNOWN_DEV_ID)
		rv = main_read8(chgnum, SM5803_REG_CHIP_ID, &dev_id);

	if (!rv)
		*id = dev_id;

	return rv;

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


	if ((reg & SM5803_FLOW1_MODE) == CHARGER_MODE_DISABLED &&
	    !(reg & SM5803_FLOW1_LINEAR_CHARGE_EN))
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}

static enum ec_error_list sm5803_set_mode(int chgnum, int mode)
{
	enum ec_error_list rv = EC_SUCCESS;

	if (mode & CHARGE_FLAG_INHIBIT_CHARGE) {
		rv = sm5803_flow1_update(chgnum, 0xFF, MASK_CLR);
		rv |= sm5803_flow2_update(chgnum, SM5803_FLOW2_AUTO_ENABLED,
					  MASK_CLR);
	}

	return rv;
}

static enum ec_error_list sm5803_get_current(int chgnum, int *current)
{
	enum ec_error_list rv;
	int reg;
	int curr;

	rv = meas_read8(chgnum, SM5803_REG_IBAT_CHG_MEAS_MSB, &reg);
	if (rv)
		return rv;
	curr = reg << 2;

	rv = meas_read8(chgnum, SM5803_REG_IBAT_CHG_MEAS_LSB, &reg);
	if (rv)
		return rv;
	curr |= reg & SM5803_IBAT_CHG_MEAS_LSB;

	/* The LSB is 7.32mA */
	*current = curr * 732 / 100;
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

	rv = meas_read8(chgnum, SM5803_REG_VSYS_MEAS_MSB, &reg);
	if (rv)
		return rv;
	volt_bits = reg << 2;

	rv = meas_read8(chgnum, SM5803_REG_VSYS_MEAS_LSB, &reg);
	if (rv)
		return rv;
	volt_bits |= reg & 0x3;

	/* The LSB is 23.4mV */
	*voltage = volt_bits * 234 / 10;

	return EC_SUCCESS;
}

static enum ec_error_list sm5803_set_voltage(int chgnum, int voltage)
{
	enum ec_error_list rv;
	int regval;
	static int attempt_bfet_enable;

	regval = SM5803_VOLTAGE_TO_REG(voltage);

	/*
	 * Note: Set both voltages on both chargers.  Vbat will only be used on
	 * primary, which enables charging.
	 */
	rv = chg_write8(chgnum, SM5803_REG_VSYS_PREREG_MSB, (regval >> 3));
	rv |= chg_write8(chgnum, SM5803_REG_VSYS_PREREG_LSB, (regval & 0x7));
	rv |= chg_write8(chgnum, SM5803_REG_VBAT_FAST_MSB, (regval >> 3));
	rv |= chg_write8(chgnum, SM5803_REG_VBAT_FAST_LSB, (regval & 0x7));

	if (IS_ENABLED(CONFIG_OCPC) && chgnum != CHARGER_PRIMARY) {
		/*
		 * Check to see if the BFET is enabled.  If not, enable it by
		 * toggling linear mode on the primary charger.  The BFET can be
		 * disabled if the system is powered up from an auxiliary charge
		 * port and the battery is dead.
		 */
		rv |= chg_read8(CHARGER_PRIMARY, SM5803_REG_LOG1, &regval);
		if (!(regval & SM5803_BATFET_ON) && !attempt_bfet_enable) {
			CPRINTS("SM5803: Attempting to turn on BFET");
			cflush();
			rv |= sm5803_flow1_update(CHARGER_PRIMARY,
						  SM5803_FLOW1_LINEAR_CHARGE_EN,
						  MASK_SET);
			rv |= sm5803_flow1_update(CHARGER_PRIMARY,
						  SM5803_FLOW1_LINEAR_CHARGE_EN,
						  MASK_CLR);
			attempt_bfet_enable = 1;
			sm5803_vbus_sink_enable(chgnum, 1);
		}
	}

	return rv;
}

static enum ec_error_list sm5803_discharge_on_ac(int chgnum, int enable)
{
	enum ec_error_list rv = EC_SUCCESS;

	if (enable) {
		rv = sm5803_vbus_sink_enable(chgnum, 0);
	} else {
		if (chgnum == charge_manager_get_active_charge_port())
			rv = sm5803_vbus_sink_enable(chgnum, 1);
	}

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
	*voltage = (volt_bits * 234) / 10;
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
	int curr;

	rv = meas_read8(chgnum, SM5803_REG_IBUS_CHG_MEAS_MSB, &reg);
	if (rv)
		return rv;
	curr = reg << 2;

	rv = meas_read8(chgnum, SM5803_REG_IBUS_CHG_MEAS_LSB, &reg);
	if (rv)
		return rv;
	curr |= reg & 0x3;

	/* The LSB is 7.32mA */
	*input_current = curr * 732 / 100;
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

	mutex_lock(&flow1_access_lock[chgnum]);

	reg = option & 0xFF;
	rv = chg_write8(chgnum, SM5803_REG_FLOW1, reg);

	mutex_unlock(&flow1_access_lock[chgnum]);
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

static enum ec_error_list sm5803_set_otg_current_voltage(int chgnum,
							 int output_current,
							 int output_voltage)
{
	enum ec_error_list rv;
	int reg;

	rv = chg_read8(chgnum, SM5803_REG_DISCH_CONF5, &reg);
	if (rv)
		return rv;

	reg &= ~SM5803_DISCH_CONF5_CLS_LIMIT;
	reg |= MIN((output_current / SM5803_CLS_CURRENT_STEP),
						SM5803_DISCH_CONF5_CLS_LIMIT);
	rv |= chg_write8(chgnum, SM5803_REG_DISCH_CONF5, reg);

	reg = SM5803_VOLTAGE_TO_REG(output_voltage);
	rv = chg_write8(chgnum, SM5803_REG_VPWR_MSB, (reg >> 3));
	rv |= chg_write8(chgnum, SM5803_REG_DISCH_CONF2,
					reg & SM5803_DISCH_CONF5_VPWR_LSB);

	return rv;
}

static enum ec_error_list sm5803_enable_otg_power(int chgnum, int enabled)
{
	enum ec_error_list rv;
	int reg;

	if (enabled) {
		int selected_current;

		rv = chg_read8(chgnum, SM5803_REG_ANA_EN1, &reg);
		if (rv)
			return rv;

		/* Enable current limit */
		reg &= ~SM5803_ANA_EN1_CLS_DISABLE;
		rv = chg_write8(chgnum, SM5803_REG_ANA_EN1, reg);

		/*
		 * In order to ensure the Vbus output doesn't overshoot too
		 * much, turn the starting voltage down to 4.8 V and ramp up
		 * after 4 ms
		 */
		rv = chg_read8(chgnum, SM5803_REG_DISCH_CONF5, &reg);
		if (rv)
			return rv;

		selected_current = (reg & SM5803_DISCH_CONF5_CLS_LIMIT) *
			SM5803_CLS_CURRENT_STEP;
		sm5803_set_otg_current_voltage(chgnum, selected_current, 4800);

		/*
		 * Enable: SOURCE_MODE - enable sourcing out
		 *	   DIRECTCHG_SOURCE_EN - enable current loop
		 *	   (for designs with no external Vbus FET)
		 */
		rv = sm5803_flow1_update(chgnum, CHARGER_MODE_SOURCE |
					 SM5803_FLOW1_DIRECTCHG_SRC_EN,
					 MASK_SET);
		usleep(4000);

		sm5803_set_otg_current_voltage(chgnum, selected_current, 5000);
	} else {
		/*
		 * PD tasks will always turn off previous sourcing on init.
		 * Protect ourselves from brown out on init by checking if we're
		 * sinking right now.  The init process should only leave sink
		 * mode enabled if a charger is plugged in; otherwise it's
		 * expected to be 0.
		 */
		rv = chg_read8(chgnum, SM5803_REG_FLOW1, &reg);
		if (rv)
			return rv;

		if ((reg & SM5803_FLOW1_MODE) != CHARGER_MODE_SINK)
			rv = sm5803_flow1_update(chgnum, CHARGER_MODE_SOURCE |
						 SM5803_FLOW1_DIRECTCHG_SRC_EN,
						 MASK_CLR);
	}

	return rv;
}

static int sm5803_is_sourcing_otg_power(int chgnum, int port)
{
	enum ec_error_list rv;
	int reg;

	rv = chg_read8(chgnum, SM5803_REG_FLOW1, &reg);
	if (rv)
		return 0;

	/*
	 * Note: In linear mode, MB charger will read a reserved mode when
	 * sourcing, so bit 1 is the most reliable way to detect sourcing.
	 */
	return (reg & BIT(1));
}

static enum ec_error_list sm5803_set_vsys_compensation(int chgnum,
						       struct ocpc_data *ocpc,
						       int current_ma,
						       int voltage_mv)
{

	int rv;
	int regval;
	int r;

	/* Set IR drop compensation */
	r = ocpc->combined_rsys_rbatt_mo * 100 / 167; /* 1.67mOhm steps */
	r = MAX(0, r);
	rv = chg_write8(chgnum, SM5803_REG_IR_COMP2, r & 0x7F);
	rv |= chg_read8(chgnum, SM5803_REG_IR_COMP1, &regval);
	regval &= ~SM5803_IR_COMP_RES_SET_MSB;
	r = r >> 8; /* Bits 9:8 */
	regval |= (r & 0x3) << SM5803_IR_COMP_RES_SET_MSB_SHIFT;
	regval |= SM5803_IR_COMP_EN;
	rv |= chg_write8(chgnum, SM5803_REG_IR_COMP1, regval);

	if (rv)
		return EC_ERROR_UNKNOWN;

	return EC_ERROR_UNIMPLEMENTED;
}

#ifdef CONFIG_CMD_CHARGER_DUMP
static int command_sm5803_dump(int argc, char **argv)
{
	int reg;
	int regval;
	int chgnum = 0;

	if (argc > 1)
		chgnum = atoi(argv[1]);

	/* Dump base regs */
	ccprintf("BASE regs\n");
	for (reg = 0x01; reg <= 0x30; reg++) {
		if (!main_read8(chgnum, reg, &regval))
			ccprintf("[0x%02X] = 0x%02x\n", reg, regval);
		if (reg & 0xf) {
			cflush(); /* Flush periodically */
			watchdog_reload();
		}
	}

	/* Dump measure regs */
	ccprintf("MEAS regs\n");
	for (reg = 0x01; reg <= 0xED; reg++) {
		if (!meas_read8(chgnum, reg, &regval))
			ccprintf("[0x%02X] = 0x%02x\n", reg, regval);
		if (reg & 0xf) {
			cflush(); /* Flush periodically */
			watchdog_reload();
		}
	}

	/* Dump Charger regs from 0x1C to 0x7F */
	ccprintf("CHG regs\n");
	for (reg = 0x1C; reg <= 0x7F; reg++) {
		if (!chg_read8(chgnum, reg, &regval))
			ccprintf("[0x%02X] = 0x%02x\n", reg, regval);
		if (reg & 0xf) {
			cflush(); /* Flush periodically */
			watchdog_reload();
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(charger_dump, command_sm5803_dump,
			"charger_dump [chgnum]", "Dumps SM5803 registers");
#endif /* CONFIG_CMD_CHARGER_DUMP */

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
	.discharge_on_ac = &sm5803_discharge_on_ac,
	.get_vbus_voltage = &sm5803_get_vbus_voltage,
	.set_input_current = &sm5803_set_input_current,
	.get_input_current = &sm5803_get_input_current,
	.device_id = &sm5803_get_dev_id,
	.get_option = &sm5803_get_option,
	.set_option = &sm5803_set_option,
	.set_otg_current_voltage = &sm5803_set_otg_current_voltage,
	.enable_otg_power = &sm5803_enable_otg_power,
	.is_sourcing_otg_power = &sm5803_is_sourcing_otg_power,
	.set_vsys_compensation = &sm5803_set_vsys_compensation,
};
