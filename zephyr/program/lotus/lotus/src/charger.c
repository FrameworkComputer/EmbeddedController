/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "board_charger.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "charge_manager.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#ifdef CONFIG_PLATFORM_EC_CHARGER_INIT_CUSTOM
static void charger_chips_init(void);
static void charger_chips_init_retry(void)
{
	charger_chips_init();
}
DECLARE_DEFERRED(charger_chips_init_retry);

static void charger_chips_init(void)
{
	int chip;
	uint16_t val = 0x0000; /*default ac setting */
	uint32_t data = 0;

	const struct battery_info *bi = battery_get_info();

	/*
	 * In our case the EC can boot before the charger has power so
	 * check if the charger is responsive before we try to init it
	 */
	if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, &data) != EC_SUCCESS) {
		CPRINTS("Retry Charger init");
		hook_call_deferred(&charger_chips_init_retry_data, 100*MSEC);
		return;
	}

	for (chip = 0; chip < board_get_charger_chip_count(); chip++) {
		if (chg_chips[chip].drv->init)
			chg_chips[chip].drv->init(chip);
	}

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL2, 
		ISL9241_CONTROL2_TRICKLE_CHG_CURR(bi->precharge_current) |
		ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL3, ISL9241_CONTROL3_ACLIM_RELOAD |
		ISL9241_CONTROL3_BATGONE))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, 0x0000))
		goto init_fail;

	val = ISL9241_CONTROL1_PROCHOT_REF_6000;
	val |= ((ISL9241_CONTROL1_SWITCHING_FREQ_656KHZ << 7) &
			ISL9241_CONTROL1_SWITCHING_FREQ_MASK);

	/* make sure battery FET is enabled on EC on */
	val &= ~ISL9241_CONTROL1_BGATE_OFF;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, val))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL4, ISL9241_CONTROL4_WOCP_FUNCTION |
		ISL9241_CONTROL4_VSYS_SHORT_CHECK |
		ISL9241_CONTROL4_ACOK_BATGONE_DEBOUNCE_25US))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_OTG_VOLTAGE, 0x0000))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_OTG_CURRENT, 0x0000))
		goto init_fail;

	/* according to Power team suggest, Set ACOK reference to 4.032V */
	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, ISL9241_MV_TO_ACOK_REFERENCE(4032)))
		goto init_fail;

	/* TODO: should we need to talk to PD chip after initial complete ? */
	CPRINTS("ISL9241 customized initial complete!");
	return;

init_fail:
	CPRINTF("ISL9241 customized initial failed!");
}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_POST_I2C + 1);
#endif

void charger_update(void)
{
	static int pre_ac_state;
	static int pre_dc_state;
	int val = 0x0000;

	if (pre_ac_state != extpower_is_present() ||
		pre_dc_state != battery_is_present()) {
		CPRINTS("update charger!!");

		if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, &val)) {
			CPRINTS("read charger control1 fail");
		}

		val = ISL9241_CONTROL1_PROCHOT_REF_6000;
		val |= ((ISL9241_CONTROL1_SWITCHING_FREQ_656KHZ << 7) &
			ISL9241_CONTROL1_SWITCHING_FREQ_MASK);

		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, val)) {
			CPRINTS("Update charger control1 fail");
		}

		/* Set DC prochot to 6.912A */
		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_DC_PROCHOT, 0x1D00))
			CPRINTS("Update DC prochot fail");

		pre_ac_state = extpower_is_present();
		pre_dc_state = battery_is_present();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);

static bool bypass_force_en;
__override int board_should_charger_bypass(void)
{
	int power_uw = charge_manager_get_power_limit_uw();

	if (bypass_force_en)
		return true;
	if (power_uw >= 100000000)
		return true;
	else
		return false;
}

/* EC console command */
static int chgbypass_cmd(int argc, const char **argv)
{
	if (argc >= 2) {
		if (!strncmp(argv[1], "en", 2)) {
			bypass_force_en = true;
		} else if (!strncmp(argv[1], "dis", 3)) {
			bypass_force_en = false;
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chargerbypass, chgbypass_cmd, "[en/dis]",
			"Force charger bypass enabled");
void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	int prochot_ma;

	if (charge_ma < CONFIG_PLATFORM_EC_CHARGER_INPUT_CURRENT) {
		charge_ma = CONFIG_PLATFORM_EC_CHARGER_INPUT_CURRENT;
	}

	/*Handle EPR converstion through the buck switcher*/
	if (charge_mv > 20000) {
		/* (charge_ma * charge_mv / 20000 ) * 0.9 */
		charge_ma = (90 * charge_ma * charge_mv / 2000000);
		CPRINTS("Updating charger with EPR correction: ma %d",charge_ma);
	} else {
		charge_ma = charge_ma * 94 / 100;
	}
	/*
	 * ac prochot should bigger than input current
	 * And needs to be at least 128mA bigger than the adapter current
	 */
	prochot_ma = (DIV_ROUND_UP(charge_ma, 855) * 855);


	if ((prochot_ma - charge_ma) < 853) {
		/* We need prochot to be at least 1 LSB above
		 * the input current limit. This is not ideal
		 * due to the low accuracy on prochot.
		 */
		prochot_ma += 853;
	}

	charge_set_input_current_limit(charge_ma, charge_mv);
	/* sync-up ac prochot with current change */
	isl9241_set_ac_prochot(0, prochot_ma);
}
