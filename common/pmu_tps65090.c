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
#include "util.h"

#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define TPS65090_I2C_ADDR 0x90

#define IRQ1_REG 0x00
#define IRQ2_REG 0x01
#define IRQ1MASK 0x02
#define IRQ2MASK 0x03
#define CG_CTRL0 0x04
#define CG_CTRL1 0x05
#define CG_CTRL2 0x06
#define CG_CTRL3 0x07
#define CG_CTRL4 0x08
#define CG_CTRL5 0x09
#define CG_STATUS1 0x0a
#define CG_STATUS2 0x0b

/* IRQ events */
#define EVENT_VACG    (1 <<  1)
#define EVENT_VBATG   (1 <<  3)

/* Charger alarm */
#define CHARGER_ALARM 3

/* Read/write tps65090 register */
static inline int pmu_read(int reg, int *value)
{
	return i2c_read8(I2C_PORT_CHARGER, TPS65090_I2C_ADDR, reg, value);
}

static inline int pmu_write(int reg, int value)
{
	return i2c_write8(I2C_PORT_CHARGER, TPS65090_I2C_ADDR, reg, value);
}

/* Clear tps65090 irq */
static inline int pmu_clear_irq(void)
{
	return pmu_write(IRQ1_REG, 0);
}

/* Read all tps65090 interrupt events */
static int pmu_get_event(int *event)
{
	static int prev_event;
	int rv;
	int irq1, irq2;

	pmu_clear_irq();

	rv = pmu_read(IRQ1_REG, &irq1);
	if (rv)
		return rv;
	rv = pmu_read(IRQ2_REG, &irq2);
	if (rv)
		return rv;

	*event = irq1 | (irq2 << 8);

	if (prev_event != *event) {
		CPRINTF("pmu event: %016b\n", *event);
		prev_event = *event;
	}

	return EC_SUCCESS;
}

int pmu_is_charger_alarm(void)
{
	int status;

	/**
	 * if the I2C access to the PMU fails, we consider the failure as
	 * non-critical and wait for the next read without send the alert.
	 */
	if (!pmu_read(CG_STATUS1, &status) && (status & CHARGER_ALARM))
		return 1;
	return 0;
}

int pmu_get_power_source(int *ac_good, int *battery_good)
{
	int rv, event;

	rv = pmu_get_event(&event);
	if (rv)
		return rv;

	if (ac_good)
		*ac_good = event & EVENT_VACG;
	if (battery_good)
		*battery_good = event & EVENT_VBATG;

	return EC_SUCCESS;
}

void pmu_init(void)
{
	/* Init configuration
	 *   Fast charge timer    : 2 hours
	 *   Charger              : disable
	 *   External pin control : enable
	 *
	 * TODO: move settings to battery pack specific init
	 */
	pmu_write(CG_CTRL0, 2);

	/* Enable interrupt mask */
	pmu_write(IRQ1MASK, 0xff);
	pmu_write(IRQ2MASK, 0xff);

	/* Limit full charge current to 50%
	 * TODO: remove this temporary hack.
	 */
	pmu_write(CG_CTRL3, 0xbb);
}
