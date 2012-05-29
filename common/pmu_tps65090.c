/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TPS65090 PMU driver.
 */

#include "board.h"
#include "console.h"
#include "common.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "smart_battery.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define TPS65090_I2C_ADDR 0x90

#define CG_CTRL0 0x04
#define CG_CTRL1 0x05
#define CG_CTRL2 0x06
#define CG_CTRL3 0x07
#define CG_CTRL4 0x08
#define CG_CTRL5 0x09
#define CG_STATUS1 0x0a
#define CG_STATUS2 0x0b

#define CHARGER_ENABLE 1
#define FASTCHARGE_SHIFT 2
#define FASTCHARGE_MASK (7 << FASTCHARGE_SHIFT)

enum FASTCHARGE_SAFETY_TIMER {
	FASTCHARGE_2HRS,
	FASTCHARGE_3HRS,
	FASTCHARGE_4HRS,
	FASTCHARGE_5HRS,
	FASTCHARGE_6HRS,
	FASTCHARGE_7HRS,
	FASTCHARGE_8HRS,
	FASTCHARGE_10HRS
};

static inline int pmu_read(int reg, int *value)
{
	int rv;

	rv = i2c_read8(I2C_PORT_CHARGER, TPS65090_I2C_ADDR, reg, value);
#ifdef CONFIG_DEBUG
	CPRINTF("%s %d %d failed\n", __func__, reg, *value);
#endif /* CONFIG_DEBUG */

	return rv;
}

static inline int pmu_write(int reg, int value)
{
	int rv;

	rv = i2c_write8(I2C_PORT_CHARGER, TPS65090_I2C_ADDR, reg, value);
#ifdef CONFIG_DEBUG
	CPRINTF("%s %d %d failed\n", __func__, reg, value);
#endif /* CONFIG_DEBUG */

	return rv;
}

static int pmu_enable_charger(int enable)
{
	int rv, d;

	rv = pmu_read(CG_CTRL0, &d);
	if (rv)
		return rv;

	if (enable)
		d |= CHARGER_ENABLE;
	else
		d &= ~CHARGER_ENABLE;

	return pmu_write(CG_CTRL0, d);
}

static int pmu_set_fastcharge_safty_timer(enum FASTCHARGE_SAFETY_TIMER stime)
{
	int rv, d;

	rv = pmu_read(CG_CTRL0, &d);
	if (rv)
		return rv;

	d &= ~FASTCHARGE_MASK;
	d |= stime << FASTCHARGE_SHIFT;

	return pmu_write(CG_CTRL0, d);
}

void pmu_init(void)
{
	/* Fast charge timer = 2 hours
	 * TODO: move this setting into battery pack file
	 */
	pmu_set_fastcharge_safty_timer(FASTCHARGE_2HRS);

	/* Enable charging  */
	pmu_enable_charger(1);
}

#ifdef CONFIG_TASK_PMU_TPS65090_CHARGER

void pmu_charger_task(void)
{
	int rv, d;
	int alarm = -1, batt_temp = -1;
	int batt_v = -1, batt_i = -1;
	int desired_v = -1, desired_i = -1;

	pmu_init();

	while (1) {
		/* Get battery alarm, voltage, current, temperature
		 * TODO: Add discharging control
		 */
		rv = battery_status(&d);
		if (!rv && alarm != d) {
			CPRINTF("[batt alarm %016b]\n", d);
			alarm = d;
		}

		rv = battery_voltage(&d);
		if (!rv && batt_v != d) {
			CPRINTF("[batt V %d mV]\n", d);
			batt_v = d;
		}

		rv = battery_current(&d);
		if (!rv && batt_i != d) {
			CPRINTF("[batt I %d mA]\n", d);
			batt_i = d;
		}

		rv = battery_desired_voltage(&d);
		if (!rv && desired_v != d) {
			CPRINTF("[batt d_V %d mV]\n", d);
			desired_v = d;
		}

		rv = battery_desired_current(&d);
		if (!rv && desired_i != d) {
			CPRINTF("[batt d_I %d mA]\n", d);
			desired_i = d;
		}

		rv = battery_temperature(&d);
		if (!rv && batt_temp != d) {
			batt_temp = d;
			CPRINTF("[batt T %d.%d C]\n",
				(d - 2731) / 10, (d - 2731) % 10);
		}
		usleep(5000000);
	}
}

#endif /* CONFIG_TASK_PMU_TPS65090_CHARGER */

