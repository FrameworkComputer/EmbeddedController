/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LM3630A LED driver.
 */

#include "hooks.h"
#include "i2c.h"
#include "lm3630a.h"
#include "timer.h"

/* I2C address */
#define LM3630A_I2C_ADDR_FLAGS 0x36

static inline int lm3630a_write(uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_KBLIGHT, LM3630A_I2C_ADDR_FLAGS, reg, val);
}

static void deferred_lm3630a_poweron(void)
{
	/*
	 * Set full brightness so that PWM will control. This needs to happen
	 * after setting the control register, because enabling the banks
	 * resets the value to 0.
	 */
	lm3630a_write(LM3630A_REG_A_BRIGHTNESS, 0xff);
}
DECLARE_DEFERRED(deferred_lm3630a_poweron);

int lm3630a_poweron(void)
{
	int ret = 0;

	/*
	 * LM3630A will NAK I2C transactions for 1ms (tWAIT in the datasheet)
	 * after HWEN asserted or after SW reset.
	 */
	crec_msleep(1);

	/* Sample PWM every 8 periods. */
	ret |= lm3630a_write(LM3630A_REG_FILTER_STRENGTH, 0x3);

	/* Enable feedback and PWM for banks A. */
	ret |= lm3630a_write(LM3630A_REG_CONFIG,
			     LM3630A_CFG_BIT_FB_EN_A |
				     LM3630A_CFG_BIT_PWM_EN_A);

	/* 24V, 800mA overcurrent protection, 500kHz boost frequency. */
	ret |= lm3630a_write(LM3630A_REG_BOOST_CONTROL,
			     LM3630A_BOOST_OVP_24V | LM3630A_BOOST_OCP_800MA |
				     LM3630A_FMODE_500KHZ);

	/* Limit current to 24.5mA */
	ret |= lm3630a_write(LM3630A_REG_A_CURRENT, 0x1a);

	/* Enable bank A, put in linear mode, and connect LED2 to bank A. */
	ret |= lm3630a_write(LM3630A_REG_CONTROL,
			     LM3630A_CTRL_BIT_LINEAR_A |
				     LM3630A_CTRL_BIT_LED_EN_A |
				     LM3630A_CTRL_BIT_LED2_ON_A);

	/*
	 * Only set the brightness after ~100 ms. Without this, LED may blink
	 * for a short duration, as the PWM sampler sometimes appears to be
	 * confused, and slowly dim from a large initial PWM input value.
	 */
	hook_call_deferred(&deferred_lm3630a_poweron_data, 100 * MSEC);

	return ret;
}

int lm3630a_poweroff(void)
{
	return lm3630a_write(LM3630A_REG_CONTROL, LM3630A_CTRL_BIT_SLEEP_CMD);
}
