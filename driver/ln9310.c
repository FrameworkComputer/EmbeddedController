/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LION Semiconductor LN-9310 switched capacitor converter.
 */

#include "common.h"
#include "console.h"
#include "ln9310.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"
#include "timer.h"

#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static int power_good;

int ln9310_power_good(void)
{
	return power_good;
}

static inline int raw_read8(int offset, int *value)
{
	return i2c_read8(ln9310_config.i2c_port,
			 ln9310_config.i2c_addr_flags,
			 offset,
			 value);
}

static inline int field_update8(int offset, int mask, int value)
{
	/* Clear mask and then set value in i2c reg value */
	return i2c_field_update8(ln9310_config.i2c_port,
				 ln9310_config.i2c_addr_flags,
				 offset,
				 mask,
				 value);
}

static void ln9310_irq_deferred(void)
{
	int status, val, pg_2to1, pg_3to1;

	status = raw_read8(LN9310_REG_INT1, &val);
	if (status) {
		CPRINTS("LN9310 reading INT1 failed");
		return;
	}

	CPRINTS("LN9310 received interrupt: 0x%x", val);
	/* Don't care other interrupts except mode change */
	if (!(val & LN9310_INT1_MODE))
		return;

	/* Check if the device is active in 2:1 or 3:1 switching mode */
	status = raw_read8(LN9310_REG_SYS_STS, &val);
	if (status) {
		CPRINTS("LN9310 reading SYS_STS failed");
		return;
	}
	CPRINTS("LN9310 system status: 0x%x", val);

	/* Either 2:1 or 3:1 mode active is treated as PGOOD */
	pg_2to1 = !!(val & LN9310_SYS_SWITCHING21_ACTIVE);
	pg_3to1 = !!(val & LN9310_SYS_SWITCHING31_ACTIVE);
	power_good = pg_2to1 || pg_3to1;
}
DECLARE_DEFERRED(ln9310_irq_deferred);

void ln9310_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ln9310_irq_deferred_data, 0);
}

static int is_battery_gt_10v(void)
{
	int status, val, gt_10v;

	CPRINTS("LN9310 checking input voltage, threshold=10V");
	/*
	 * Turn on INFET_OUT_SWITCH_OK comparator;
	 * configure INFET_OUT_SWITCH_OK to 10V.
	 */
	field_update8(LN9310_REG_TRACK_CTRL,
		      LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_MASK |
				LN9310_TRACK_INFET_OUT_SWITCH_OK_CFG_MASK,
		      LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_ON |
				LN9310_TRACK_INFET_OUT_SWITCH_OK_CFG_10V);

	/* Read INFET_OUT_SWITCH_OK comparator */
	status = raw_read8(LN9310_REG_BC_STS_B, &val);
	if (status) {
		CPRINTS("LN9310 reading BC_STS_B failed");
		return -1;
	}
	CPRINTS("LN9310 BC_STS_B: 0x%x", val);

	/*
	 * If INFET_OUT_SWITCH_OK=0, VIN < 10V
	 * If INFET_OUT_SWITCH_OK=1, VIN > 10V
	 */
	gt_10v = !!(val & LN9310_BC_STS_B_INFET_OUT_SWITCH_OK);
	CPRINTS("LN9310 battery %s 10V", gt_10v ? ">" : "<");

	/* Turn off INFET_OUT_SWITCH_OK comparator */
	field_update8(LN9310_REG_TRACK_CTRL,
		      LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_MASK,
		      LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_OFF);

	return gt_10v;
}

static int ln9310_init_3to1(void)
{
	CPRINTS("LN9310 init (3:1 operation)");

	/* Enable track protection and SC_OUT configs for 3:1 switching */
	field_update8(LN9310_REG_MODE_CHANGE_CFG,
		      LN9310_MODE_TM_TRACK_MASK |
				LN9310_MODE_TM_SC_OUT_PRECHG_MASK |
				LN9310_MODE_TM_VIN_OV_CFG_MASK,
		      LN9310_MODE_TM_TRACK_SWITCH31 |
				LN9310_MODE_TM_SC_OUT_PRECHG_SWITCH31 |
				LN9310_MODE_TM_VIN_OV_CFG_3S);

	/* Enable 3:1 operation mode */
	field_update8(LN9310_REG_PWR_CTRL,
		      LN9310_PWR_OP_MODE_MASK,
		      LN9310_PWR_OP_MODE_SWITCH31);

	/* 3S lower bounde delta configurations */
	field_update8(LN9310_REG_LB_CTRL,
		      LN9310_LB_DELTA_MASK,
		      LN9310_LB_DELTA_3S);

	/*
	 * TODO(waihong): The LN9310_REG_SYS_CTR was set to a wrong value
	 * accidentally. Override it to 0. This may not need.
	 */
	field_update8(LN9310_REG_SYS_CTRL,
		      0xff,
		      0);

	return EC_SUCCESS;
}

static int ln9310_init_2to1(void)
{
	CPRINTS("LN9310 init (2:1 operation)");

	if (is_battery_gt_10v()) {
		CPRINTS("LN9310 init stop. Input voltage is too high.");
		return EC_ERROR_UNKNOWN;
	}

	/* Enable track protection and SC_OUT configs for 2:1 switching */
	field_update8(LN9310_REG_MODE_CHANGE_CFG,
		      LN9310_MODE_TM_TRACK_MASK |
				LN9310_MODE_TM_SC_OUT_PRECHG_MASK,
		      LN9310_MODE_TM_TRACK_SWITCH21 |
				LN9310_MODE_TM_SC_OUT_PRECHG_SWITCH21);

	/* Enable 2:1 operation mode */
	field_update8(LN9310_REG_PWR_CTRL,
		      LN9310_PWR_OP_MODE_MASK,
		      LN9310_PWR_OP_MODE_SWITCH21);

	/* 2S lower bounde delta configurations */
	field_update8(LN9310_REG_LB_CTRL,
		      LN9310_LB_DELTA_MASK,
		      LN9310_LB_DELTA_2S);

	/*
	 * TODO(waihong): The LN9310_REG_SYS_CTR was set to a wrong value
	 * accidentally. Override it to 0. This may not need.
	 */
	field_update8(LN9310_REG_SYS_CTRL,
		      0xff,
		      0);

	return EC_SUCCESS;
}

void ln9310_init(void)
{
	int status, val;
	enum battery_cell_type batt;

	/*
	 * Set OPERATION_MODE update method
	 *   - OP_MODE_MANUAL_UPDATE = 0
	 *   - OP_MODE_SELF_SYNC_EN  = 1
	 */
	field_update8(LN9310_REG_PWR_CTRL,
		      LN9310_PWR_OP_MODE_MANUAL_UPDATE_MASK,
		      LN9310_PWR_OP_MODE_MANUAL_UPDATE_OFF);

	field_update8(LN9310_REG_TIMER_CTRL,
		      LN9310_TIMER_OP_SELF_SYNC_EN_MASK,
		      LN9310_TIMER_OP_SELF_SYNC_EN_ON);

	usleep(LN9310_CDC_DELAY);
	CPRINTS("LN9310 OP_MODE Update method: Self-sync");

	batt = board_get_battery_cell_type();
	if (batt == BATTERY_CELL_TYPE_3S) {
		status = ln9310_init_3to1();
	} else if (batt == BATTERY_CELL_TYPE_2S) {
		status = ln9310_init_2to1();
	} else {
		CPRINTS("LN9310 not supported battery type: %d", batt);
		return;
	}

	if (status != EC_SUCCESS)
		return;

	/* Unmask the MODE change interrupt */
	field_update8(LN9310_REG_INT1_MSK,
		      LN9310_INT1_MODE,
		      0);

	/* Dummy clear all interrupts */
	status = raw_read8(LN9310_REG_INT1, &val);
	if (status) {
		CPRINTS("LN9310 reading INT1 failed");
		return;
	}

	/* Clear the STANDBY_EN bit */
	field_update8(LN9310_REG_STARTUP_CTRL,
		      LN9310_STANDBY_EN,
		      0);

	CPRINTS("LN9310 cleared interrupts: 0x%x", val);
}
