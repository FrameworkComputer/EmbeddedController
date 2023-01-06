/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "board_charger.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"

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
		ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR |
		ISL9241_CONTROL2_PROCHOT_DEBOUNCE_1000))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL3, ISL9241_CONTROL3_PSYS_GAIN |
			ISL9241_CONTROL3_ACLIM_RELOAD))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, 0x0000))
		goto init_fail;

	val = ISL9241_CONTROL1_PROCHOT_REF_6800;
	val |= ((ISL9241_CONTROL1_SWITCHING_FREQ_724KHZ << 7) &
			ISL9241_CONTROL1_SWITCHING_FREQ_MASK);

	/* make sure battery FET is enabled on EC on */
	val &= ~ISL9241_CONTROL1_BGATE_OFF;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, val))
		goto init_fail;

	/* according to Power team suggest, Set ACOK reference to 4.544V */
	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, 0x0B00))
		goto init_fail;

	/* TODO: should we need to talk to PD chip after initial complete ? */
	return;

init_fail:
	CPRINTF("ISL9241 customized initial failed!");
}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_POST_I2C + 1);
#endif

static void charger_enable_psys(void)
{
	int control1;

	if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, &control1))
		CPRINTS("read psys control1 fail");

	control1 &= ~ISL9241_CONTROL1_IMON;
	control1 |= ISL9241_CONTROL1_PSYS;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, 0x0B00))
		CPRINTS("Update ACOK reference fail");

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, control1))
		CPRINTS("Update psys control1 fail");

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL4, 0x0000))
		CPRINTS("Update psys control4 fail");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, charger_enable_psys, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, charger_enable_psys, HOOK_PRIO_DEFAULT);

static void charger_disable_psys(void)
{
	int control1;

	if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, &control1))
		CPRINTS("read psys control1 fail");

	control1 |= ISL9241_CONTROL1_IMON;
	control1 &= ~ISL9241_CONTROL1_PSYS;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, 0x0000))
		CPRINTS("Update ACOK reference fail");

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, control1))
		CPRINTS("Update psys control1 fail");

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL4, ISL9241_CONTROL4_GP_COMPARATOR))
		CPRINTS("Update psys control4 fail");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, charger_disable_psys, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, charger_disable_psys, HOOK_PRIO_DEFAULT);

void charger_update(void)
{
	static int pre_ac_state;
	static int pre_dc_state;
	int val = 0x0000;

	if (pre_ac_state != extpower_is_present() ||
		pre_dc_state != battery_is_present())
	{
		CPRINTS("update charger!!");

		if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, &val)) {
			CPRINTS("read charger control1 fail");
		}

		val = ISL9241_CONTROL1_PROCHOT_REF_6800;
		val |= ((ISL9241_CONTROL1_SWITCHING_FREQ_724KHZ << 7) &
			ISL9241_CONTROL1_SWITCHING_FREQ_MASK);

		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, val)) {
			CPRINTS("Update charger control1 fail");
		}

		/* Set DC prochot to 6.912A */
		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_DC_PROCHOT, 0x1B00))
			CPRINTS("Update DC prochot fail");

		pre_ac_state = extpower_is_present();
		pre_dc_state = battery_is_present();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);

void charger_gate_onoff(uint8_t enable)
{
	int control0 = 0x0000;
	int control1 = 0x0000;

	if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, &control0)) {
		CPRINTS("read gate control1 fail");
	}

	if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, &control1)) {
		CPRINTS("read gate control1 fail");
	}

	if (enable) {
		control0 &= ~ISL9241_CONTROL0_NGATE_OFF;
		control1 &= ~ISL9241_CONTROL1_BGATE_OFF;
		CPRINTS("B&N Gate off");
	} else {
		control0 |= ISL9241_CONTROL0_NGATE_OFF;
		control1 |= ISL9241_CONTROL1_BGATE_OFF;
		CPRINTS("B&N Gate on");
	}

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, control0)) {
		CPRINTS("Update gate control0 fail");
	}

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, control1)) {
		CPRINTS("Update gate control1 fail");
	}
}
