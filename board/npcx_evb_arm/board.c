/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Nuvoton M4 EB configuration */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "chipset.h"
#include "common.h"
#include "driver/temp_sensor/tmp006.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "peci.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "timer.h"
#include "thermal.h"
#include "util.h"
#include "shi_chip.h"

#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_CH_0] = {"ADC0", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	[ADC_CH_1] = {"ADC1", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	[ADC_CH_2] = {"ADC2", NPCX_ADC_CH2, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = { 0, PWM_CONFIG_DSLEEP_CLK, 100},
	[PWM_CH_KBLIGHT] = { 1, 0, 10000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.flags = FAN_USE_RPM_MODE,
		.rpm_min = 1000,
		.rpm_start = 1000,
		.rpm_max = 5200,
		.ch = 0,/* Use MFT id to control fan */
		.pgood_gpio = GPIO_PGOOD_FAN,
		.enable_gpio = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/******************************************************************************/
/* MFT channels. These are logically separate from mft_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_1, TCKC_LFCLK, PWM_CH_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master0-0", NPCX_I2C_PORT0_0, 100, GPIO_I2C0_SCL0, GPIO_I2C0_SDA0},
	{"master0-1", NPCX_I2C_PORT0_1, 100, GPIO_I2C0_SCL1, GPIO_I2C0_SDA1},
	{"master1",   NPCX_I2C_PORT1,   100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"master2",   NPCX_I2C_PORT2,   100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"master3",   NPCX_I2C_PORT3,   100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 40,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xf6, 0x55, 0xfa, 0xc8  /* full set */
	},
};
