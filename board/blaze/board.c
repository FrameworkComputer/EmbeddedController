/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Blaze board-specific configuration */

#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "power.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "util.h"
#include "timer.h"
#include "charger.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTON_L", GPIO_B, (1<<5),  GPIO_INT_BOTH,
	 power_button_interrupt},
	{"XPSHOLD",     GPIO_A, (1<<3),  GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_BOTH, lid_interrupt},
	{"SUSPEND_L",   GPIO_C, (1<<7),  GPIO_KB_INPUT,
	 power_signal_interrupt},
	{"SPI1_NSS",    GPIO_A, (1<<4),  GPIO_INT_BOTH | GPIO_PULL_UP,
	 spi_event},
	{"AC_PRESENT",  GPIO_A, (1<<0),  GPIO_INT_BOTH, extpower_interrupt},
	{"KB_IN00",     GPIO_C, (1<<8),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN01",     GPIO_C, (1<<9),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN02",     GPIO_C, (1<<10), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN03",     GPIO_C, (1<<11), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN04",     GPIO_C, (1<<12), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN05",     GPIO_C, (1<<14), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN06",     GPIO_C, (1<<15), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN07",     GPIO_D, (1<<2),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	/* Other inputs */
	{"WP_L",        GPIO_B, (1<<4),  GPIO_INPUT, NULL},
	/* Outputs */
	{"AP_RESET_L",  GPIO_B, (1<<3),  GPIO_ODR_HIGH, NULL},
	{"CHARGER_EN",  GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EC_INT",      GPIO_B, (1<<9),  GPIO_ODR_HIGH, NULL},
	{"ENTERING_RW", GPIO_H, (1<<0),  GPIO_OUT_LOW, NULL},
	{"I2C1_SCL",    GPIO_B, (1<<6),  GPIO_ODR_HIGH, NULL},
	{"I2C1_SDA",    GPIO_B, (1<<7),  GPIO_ODR_HIGH, NULL},
	{"LED_POWER_L", GPIO_A, (1<<2),  GPIO_OUT_HIGH, NULL},  /* PWR_LED1 */
	{"PMIC_PWRON_L", GPIO_A, (1<<12), GPIO_OUT_HIGH, NULL},
	{"PMIC_RESET",  GPIO_A, (1<<15), GPIO_OUT_LOW, NULL},
	{"KB_OUT00",    GPIO_B, (1<<0),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT01",    GPIO_B, (1<<8),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT02",    GPIO_B, (1<<12), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT03",    GPIO_B, (1<<13), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT04",    GPIO_B, (1<<14), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT05",    GPIO_B, (1<<15), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT06",    GPIO_C, (1<<0),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT07",    GPIO_C, (1<<1),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT08",    GPIO_C, (1<<2),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT09",    GPIO_B, (1<<1),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT10",    GPIO_C, (1<<5),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT11",    GPIO_C, (1<<4),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT12",    GPIO_A, (1<<13), GPIO_KB_OUTPUT, NULL},
	{"AC_LED",      GPIO_B, (1<<10), GPIO_OUT_LOW, NULL},
	{"CHG_LED",     GPIO_B, (1<<11), GPIO_OUT_LOW, NULL},
	{"BAT_LED1",    GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"CHARGING",    GPIO_A, (1<<11), GPIO_OUT_LOW, NULL},
	{"EC_BL_OVERRIDE", GPIO_H, (1<<1), GPIO_ODR_HIGH, NULL},
	{"PMIC_THERM_L",  GPIO_A, (1<<1),  GPIO_ODR_HIGH, NULL},
	{"PMIC_WARM_RESET_L", GPIO_C, (1<<3),  GPIO_ODR_HIGH, NULL},
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_A, 0x00f0, GPIO_ALT_SPI,   MODULE_SPI, GPIO_DEFAULT},
	{GPIO_A, 0x0600, GPIO_ALT_USART, MODULE_UART, GPIO_DEFAULT},
	{GPIO_B, 0x00c0, GPIO_ALT_I2C,	 MODULE_I2C, GPIO_DEFAULT},
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_SOC1V8_XPSHOLD, 1, "XPSHOLD"},
	{GPIO_SUSPEND_L,      0, "SUSPEND#_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{STM32_TIM(2), STM32_TIM_CH(3),
	 PWM_CONFIG_ACTIVE_LOW, GPIO_LED_POWER_L},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
}
