/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/bq25710.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charger, LOG_LEVEL_INF);

static void write_reg(int reg, int val)
{
	int chgnum = charge_get_active_chg_chip();
	int rv = bq257x0_set_option_reg(chgnum, reg, val);

	if (rv)
		LOG_DBG("Failed to set reg 0x%02x (rv=%d)\n", reg, rv);
	else
		LOG_DBG("Reg 0x%02x set to 0x%04x\n", reg, val);
}

/*
 * Configure charger registers required for ruby project.
 * These settings enable the Charger and IMVP PSYS functions
 * and apply the necessary parameters for a 4S1P battery.
 */
static void set_chg_reg_custom(void)
{
	/*Enables several OCP/ACOC-related protection and threshold bits*/
	write_reg(BQ25710_REG_CHARGE_OPTION_2, 0x0037);

	/*Sets IDCHG_DEG2 deglitch time to ~1.6 ms*/
	write_reg(BQ25720_REG_CHARGE_OPTION_4, 0x0040);

	/* Set VSYS VAP threshold to max (9.5V) */
	write_reg(BQ25720_REG_VMIN_ACTIVE_PROTECTION, 0x00fc);

	/*Sets minimum system voltage threshold to ~14.336 V*/
	write_reg(BQ25710_REG_MIN_SYSTEM_VOLTAGE, 0x7800);

	/*Sets adapter/input voltage detection threshold to 4.096 V*/
	write_reg(BQ25710_REG_INPUT_VOLTAGE, 0x0240);
}

DECLARE_HOOK(HOOK_INIT, set_chg_reg_custom, HOOK_PRIO_POST_BATTERY_INIT + 1);
