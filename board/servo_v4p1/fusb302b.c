/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fusb302b.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpanders.h"
#include "task.h"
#include "time.h"
#include "usb_pd.h"
#include "util.h"

static int port;
static int status0;
static int status1;
static int interrupt;
static struct mutex measure_lock;

static int tcpc_write(int reg, int val)
{
	return i2c_write8(port, FUSB302_I2C_ADDR_FLAGS, reg, val);
}

static int tcpc_read(int reg, int *val)
{
	return i2c_read8(port, FUSB302_I2C_ADDR_FLAGS, reg, val);
}

int init_fusb302b(int p)
{
	int ret;
	int reg;
	int interrupta;
	int interruptb;

	/* Configure fusb302b for SNK only operation */
	port = p;

	ret = tcpc_write(TCPC_REG_RESET, TCPC_REG_RESET_SW_RESET);
	if (ret)
		return ret;

	/* Create interrupt masks */
	reg = 0xFF;
	/* CC level changes */
	reg &= ~TCPC_REG_MASK_BC_LVL;
	/* misc alert */
	reg &= ~TCPC_REG_MASK_ALERT;
	/* VBUS threshold crossed (~4.0V) */
	reg &= ~TCPC_REG_MASK_VBUSOK;
	tcpc_write(TCPC_REG_MASK, reg);

	/* Interrupt Enable */
	ret = tcpc_read(TCPC_REG_CONTROL0, &reg);
	if (ret)
		return ret;

	reg &= ~TCPC_REG_CONTROL0_INT_MASK;
	ret = tcpc_write(TCPC_REG_CONTROL0, reg);
	if (ret)
		return ret;

	ret = tcpc_write(TCPC_REG_POWER, TCPC_REG_POWER_PWR_ALL);
	if (ret)
		return ret;

	/* reading interrupt registers clears them */
	ret = tcpc_read(TCPC_REG_INTERRUPT, &interrupt);
	if (ret)
		return ret;

	ret = tcpc_read(TCPC_REG_INTERRUPTA, &interrupta);
	if (ret)
		return ret;

	ret = tcpc_read(TCPC_REG_INTERRUPTB, &interruptb);
	if (ret)
		return ret;

	/* Call this, will detect a charger that's already plugged in */
	update_status_fusb302b();

	/* Enable interrupt */
	gpio_enable_interrupt(GPIO_CHGSRV_TCPC_INT_ODL);

	return EC_SUCCESS;
}

void fusb302b_irq(void)
{
	tcpc_read(TCPC_REG_INTERRUPT, &interrupt);
	tcpc_read(TCPC_REG_STATUS0, &status0);
	tcpc_read(TCPC_REG_STATUS1, &status1);

	task_wake(TASK_ID_PD_C2);
}
DECLARE_DEFERRED(fusb302b_irq);

int update_status_fusb302b(void)
{
	hook_call_deferred(&fusb302b_irq_data, 0);
	return EC_SUCCESS;
}

int is_vbus_present(void)
{
	return (status0 & 0x80);
}

/* Convert BC LVL values (in FUSB302) to Type-C CC Voltage Status */
static int convert_bc_lvl(int bc_lvl)
{
	int ret;

	switch (bc_lvl) {
	case 1:
		ret = TYPEC_CC_VOLT_RP_DEF;
		break;
	case 2:
		ret = TYPEC_CC_VOLT_RP_1_5;
		break;
	case 3:
		ret = TYPEC_CC_VOLT_RP_3_0;
		break;
	default:
		ret = TYPEC_CC_VOLT_OPEN;
	}

	return ret;
}

int get_cc(int *cc1, int *cc2)
{
	int reg;
	int orig_meas_cc1;
	int orig_meas_cc2;
	int bc_lvl_cc1;
	int bc_lvl_cc2;

	mutex_lock(&measure_lock);

	/*
	 * Measure CC1 first.
	 */
	tcpc_read(TCPC_REG_SWITCHES0, &reg);

	/* save original state to be returned to later... */
	if (reg & TCPC_REG_SWITCHES0_MEAS_CC1)
		orig_meas_cc1 = 1;
	else
		orig_meas_cc1 = 0;

	if (reg & TCPC_REG_SWITCHES0_MEAS_CC2)
		orig_meas_cc2 = 1;
	else
		orig_meas_cc2 = 0;

	/* Disable CC2 measurement switch, enable CC1 measurement switch */
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC2;
	reg |= TCPC_REG_SWITCHES0_MEAS_CC1;

	tcpc_write(TCPC_REG_SWITCHES0, reg);

	/* CC1 is now being measured by FUSB302. */

	/* Wait on measurement */
	crec_usleep(250);

	tcpc_read(TCPC_REG_STATUS0, &bc_lvl_cc1);

	/* mask away unwanted bits */
	bc_lvl_cc1 &= (TCPC_REG_STATUS0_BC_LVL0 | TCPC_REG_STATUS0_BC_LVL1);

	/*
	 * Measure CC2 next.
	 */

	tcpc_read(TCPC_REG_SWITCHES0, &reg);

	/* Disable CC1 measurement switch, enable CC2 measurement switch */
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC1;
	reg |= TCPC_REG_SWITCHES0_MEAS_CC2;

	tcpc_write(TCPC_REG_SWITCHES0, reg);

	/* CC2 is now being measured by FUSB302. */

	/* Wait on measurement */
	crec_usleep(250);

	tcpc_read(TCPC_REG_STATUS0, &bc_lvl_cc2);

	/* mask away unwanted bits */
	bc_lvl_cc2 &= (TCPC_REG_STATUS0_BC_LVL0 | TCPC_REG_STATUS0_BC_LVL1);

	*cc1 = convert_bc_lvl(bc_lvl_cc1);
	*cc2 = convert_bc_lvl(bc_lvl_cc2);

	/* return MEAS_CC1/2 switches to original state */
	tcpc_read(TCPC_REG_SWITCHES0, &reg);
	if (orig_meas_cc1)
		reg |= TCPC_REG_SWITCHES0_MEAS_CC1;
	else
		reg &= ~TCPC_REG_SWITCHES0_MEAS_CC1;
	if (orig_meas_cc2)
		reg |= TCPC_REG_SWITCHES0_MEAS_CC2;
	else
		reg &= ~TCPC_REG_SWITCHES0_MEAS_CC2;

	tcpc_write(TCPC_REG_SWITCHES0, reg);

	mutex_unlock(&measure_lock);
	return 0;
}
