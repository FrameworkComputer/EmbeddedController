/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nuvoton M4 EB board-specific configuration */

#include "adc.h"
#include "backlight.h"
#include "chipset.h"
#include "common.h"
#include "driver/temp_sensor/tmp112.h"
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
#include "spi.h"
#include "switch.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "timer.h"
#include "thermal.h"
#include "util.h"

#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_CH_0] = { "ADC0", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_1] = { "ADC1", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_2] = { "ADC2", NPCX_ADC_CH2, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_3] = { "ADC3", NPCX_ADC_CH3, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_4] = { "ADC4", NPCX_ADC_CH4, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_5] = { "ADC5", NPCX_ADC_CH5, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_6] = { "ADC6", NPCX_ADC_CH6, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_7] = { "ADC7", NPCX_ADC_CH7, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_8] = { "ADC8", NPCX_ADC_CH8, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_9] = { "ADC9", NPCX_ADC_CH9, ADC_MAX_VOLT, ADC_READ_MAX + 1,
		       0 },
	[ADC_CH_10] = { "ADC10", NPCX_ADC_CH10, ADC_MAX_VOLT, ADC_READ_MAX + 1,
			0 },
	[ADC_CH_11] = { "ADC11", NPCX_ADC_CH11, ADC_MAX_VOLT, ADC_READ_MAX + 1,
			0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = { 0, PWM_CONFIG_OPEN_DRAIN, 25000 },
	[PWM_CH_KBLIGHT] = { 2, 0, 10000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = 0, /* Use MFT id to control fan */
	.pgood_gpio = GPIO_PGOOD_FAN,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1000,
	.rpm_start = 1000,
	.rpm_max = 5200,
};

const struct fan_t fans[] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/******************************************************************************/
/* TMP112 sensors. Must be in the exactly same order as in enum tmp112_sensor */
const struct tmp112_sensor_t tmp112_sensors[] = {
	{ I2C_PORT_THERMAL, TMP112_I2C_ADDR_FLAGS0 },
};
BUILD_ASSERT(ARRAY_SIZE(tmp112_sensors) == TMP112_COUNT);

/******************************************************************************/
/* Temperature sensor. */
const struct temp_sensor_t temp_sensors[] = {
	{ "System", TEMP_SENSOR_TYPE_BOARD, tmp112_get_val_k, TMP112_0 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_1, TCKC_LFCLK, PWM_CH_FAN },
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "master0-0",
	  .port = NPCX_I2C_PORT0_0,
	  .kbps = 100,
	  .scl = GPIO_I2C0_SCL0,
	  .sda = GPIO_I2C0_SDA0 },
	{ .name = "master1-0",
	  .port = NPCX_I2C_PORT1_0,
	  .kbps = 100,
	  .scl = GPIO_I2C1_SCL0,
	  .sda = GPIO_I2C1_SDA0 },
	{ .name = "master2-0",
	  .port = NPCX_I2C_PORT2_0,
	  .kbps = 100,
	  .scl = GPIO_I2C2_SCL0,
	  .sda = GPIO_I2C2_SDA0 },
	{ .name = "master3-0",
	  .port = NPCX_I2C_PORT3_0,
	  .kbps = 100,
	  .scl = GPIO_I2C3_SCL0,
	  .sda = GPIO_I2C3_SDA0 },
	{ .name = "master7-0",
	  .port = NPCX_I2C_PORT7_0,
	  .kbps = 100,
	  .scl = GPIO_I2C7_SCL0,
	  .sda = GPIO_I2C7_SDA0 },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
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
