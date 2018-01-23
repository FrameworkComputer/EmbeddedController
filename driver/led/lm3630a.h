/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LM3630A LED driver.
 */

#ifndef __CROS_EC_LM3630A_H
#define __CROS_EC_LM3630A_H

#define LM3630A_REG_CONTROL		0x00
#define LM3630A_REG_CONFIG		0x01
#define LM3630A_REG_BOOST_CONTROL	0x02
#define LM3630A_REG_A_BRIGHTNESS	0x03
#define LM3630A_REG_B_BRIGHTNESS	0x04
#define LM3630A_REG_A_CURRENT		0x05
#define LM3630A_REG_B_CURRENT		0x06
#define LM3630A_REG_ONOFF_RAMP		0x07
#define LM3630A_REG_RUN_RAMP		0x08
#define LM3630A_REG_INT_STATUS		0x09
#define LM3630A_REG_INT_ENABLE		0x0a
#define LM3630A_REG_FAULT_STATUS	0x0b
#define LM3630A_REG_SW_RESET		0x0f
#define LM3630A_REG_PWM_OUT_LOW		0x12
#define LM3630A_REG_PWM_OUT_HIGH	0x13
#define LM3630A_REG_REVISION		0x1f
#define LM3630A_REG_FILTER_STRENGTH	0x50

/* Control register bits */
#define LM3630A_CTRL_BIT_SLEEP_CMD	(1 << 7)
#define LM3630A_CTRL_BIT_SLEEP_STAT	(1 << 6)
#define LM3630A_CTRL_BIT_LINEAR_A	(1 << 4)
#define LM3630A_CTRL_BIT_LINEAR_B	(1 << 3)
#define LM3630A_CTRL_BIT_LED_EN_A	(1 << 2)
#define LM3630A_CTRL_BIT_LED_EN_B	(1 << 1)
#define LM3630A_CTRL_BIT_LED2_ON_A	(1 << 0)

/* Config register bits */
#define LM3630A_CFG_BIT_FB_EN_B		(1 << 4)
#define LM3630A_CFG_BIT_FB_EN_A		(1 << 3)
#define LM3630A_CFG_BIT_PWM_LOW		(1 << 2)
#define LM3630A_CFG_BIT_PWM_EN_B	(1 << 1)
#define LM3630A_CFG_BIT_PWM_EN_A	(1 << 0)

/* Boost control register bits */
#define LM3630A_BOOST_OVP_16V		(0 << 5)
#define LM3630A_BOOST_OVP_24V		(1 << 5)
#define LM3630A_BOOST_OVP_32V		(2 << 5)
#define LM3630A_BOOST_OVP_40V		(3 << 5)
#define LM3630A_BOOST_OCP_600MA		(0 << 3)
#define LM3630A_BOOST_OCP_800MA		(1 << 3)
#define LM3630A_BOOST_OCP_1000MA	(2 << 3)
#define LM3630A_BOOST_OCP_1200MA	(3 << 3)
#define LM3630A_BOOST_SLOW_START	(1 << 2)
#define LM3630A_SHIFT_500KHZ		(0 << 1)  /* FMODE=0 */
#define LM3630A_SHIFT_560KHZ		(1 << 1)  /* FMODE=0 */
#define LM3630A_SHIFT_1000KHZ		(0 << 1)  /* FMODE=1 */
#define LM3630A_SHIFT_1120KHZ		(1 << 1)  /* FMODE=1 */
#define LM3630A_FMODE_500KHZ		(0 << 0)
#define LM3630A_FMODE_1000KHZ		(1 << 0)

/* Power on and initialize LM3630A. */
int lm3630a_poweron(void);

/* Power off LM3630A. */
int lm3630a_poweroff(void);

#endif /* __CROS_EC_LM3630A_H */
