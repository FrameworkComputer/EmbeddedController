/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Kirby board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "chipset.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_PWR_ON_L",   GPIO_A, (1<<0),  GPIO_INT_BOTH, power_interrupt},
	{"XPSHOLD",       GPIO_E, (1<<5),  GPIO_INT_RISING, power_interrupt},
	{"CHARGER_INT_L", GPIO_E, (1<<2),  GPIO_INT_FALLING,
	 NULL /*pmu_irq_handler*/},
	{"USB_CHG_INT",   GPIO_E, (1<<15), GPIO_INT_FALLING,
	 extpower_interrupt},
	{"USBPD_INT_L",   GPIO_E, (1<<1),  GPIO_INT_FALLING,
	 NULL /*extpower_interrupt*/},
	{"LID_OPEN",      GPIO_E, (1<<6),  GPIO_INT_BOTH, lid_interrupt},
	{"SUSPEND_L",     GPIO_E, (1<<3),  GPIO_INT_BOTH, power_interrupt},
	{"SPI1_NSS",      GPIO_A, (1<<4),  GPIO_INT_BOTH | GPIO_PULL_UP,
	 spi_event},
	{"AC_PRESENT_L",  GPIO_A, (1<<1),  GPIO_INT_BOTH, extpower_interrupt},
	{"KB_IN00",       GPIO_E, (1<<7),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN01",       GPIO_E, (1<<8),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN02",       GPIO_E, (1<<9),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN03",       GPIO_E, (1<<10), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN04",       GPIO_E, (1<<11), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN05",       GPIO_E, (1<<12), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN06",       GPIO_E, (1<<13), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN07",       GPIO_E, (1<<14), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	/* Other inputs */
	{"WP_L",          GPIO_B, (1<<3),  GPIO_INPUT, NULL},
	/* Outputs */
	{"CHARGER_EN_L",  GPIO_B, (1<<8),  GPIO_OUT_HIGH, NULL},
	{"EC_INT",        GPIO_C, (1<<9),  GPIO_ODR_HIGH, NULL},
	{"EC_INT2",       GPIO_B, (1<<4),  GPIO_ODR_HIGH, NULL},
	{"ID_MUX",        GPIO_B, (1<<11), GPIO_ODR_LOW, NULL},
	{"BCHGR_OTG",     GPIO_B, (1<<12), GPIO_OUT_LOW, NULL},
	{"BCHGR_PSEL",    GPIO_B, (1<<14), GPIO_OUT_LOW, NULL},
	{"EN_PP3300",     GPIO_C, (1<<11), GPIO_OUT_LOW, NULL},
	{"ENTERING_RW",   GPIO_B, (1<<15), GPIO_OUT_LOW, NULL},
	{"BST_LED_EN",    GPIO_C, (1<<10), GPIO_OUT_LOW, NULL},
	{"I2C1_SCL",      GPIO_B, (1<<6),  GPIO_ODR_HIGH, NULL},
	{"I2C1_SDA",      GPIO_B, (1<<7),  GPIO_ODR_HIGH, NULL},
	{"CHG_LED_Y",     GPIO_C, (1<<6),  GPIO_OUT_HIGH, NULL},
	{"CHG_LED_G",     GPIO_C, (1<<7),  GPIO_OUT_HIGH, NULL},
	{"CHG_LED_R",     GPIO_C, (1<<8),  GPIO_OUT_HIGH, NULL},
	{"PMIC_PWRON",    GPIO_C, (1<<12), GPIO_OUT_LOW, NULL},
	{"KB_OUT00",      GPIO_D, (1<<0),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT01",      GPIO_D, (1<<1),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT02",      GPIO_D, (1<<2),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT03",      GPIO_D, (1<<3),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT04",      GPIO_D, (1<<4),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT05",      GPIO_D, (1<<5),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT06",      GPIO_D, (1<<6),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT07",      GPIO_D, (1<<7),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT08",      GPIO_D, (1<<8),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT09",      GPIO_D, (1<<9),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT10",      GPIO_D, (1<<10), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT11",      GPIO_D, (1<<11), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT12",      GPIO_D, (1<<12), GPIO_KB_OUTPUT, NULL},
	/*
	 * TODO(crosbug.com/p/23802): These pins are never used for I2C on
	 * Kirby, but the I2C code won't currently compile unless they're
	 * defined.
	 */
	GPIO_SIGNAL_NOT_IMPLEMENTED("I2C2_SCL"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("I2C2_SDA"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_C, 0x01c0, GPIO_ALT_TIM3_4, MODULE_LED_KIRBY},
	{GPIO_A, 0x00f0, GPIO_ALT_SPI,    MODULE_SPI},
	{GPIO_A, 0x0600, GPIO_ALT_USART,  MODULE_UART},
	{GPIO_B, 0x00c0, GPIO_ALT_I2C,	  MODULE_I2C},
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{STM32_TIM(3), STM32_TIM_CH(1), PWM_CONFIG_ACTIVE_LOW,
			  GPIO_CHG_LED_Y},
	{STM32_TIM(3), STM32_TIM_CH(2), PWM_CONFIG_ACTIVE_LOW,
			  GPIO_CHG_LED_G},
	{STM32_TIM(3), STM32_TIM_CH(3), PWM_CONFIG_ACTIVE_LOW,
			  GPIO_CHG_LED_R},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/*
	 * VBUS voltage sense pin.
	 * Sense pin 1.8V is converted to 4096. Accounting for the 3x
	 * voltage divider, the conversion factor is 5400mV/4096.
	 */
	[ADC_CH_USB_VBUS_SNS] = {"USB_VBUS_SNS", 5400, 4096, 0, STM32_AIN(12)},
	/*
	 * Micro USB D+ sense pin. Voltage divider = 2/3.
	 * Converted to mV (2700mV/4096).
	 */
	[ADC_CH_USB_DP_SNS] = {"USB_DP_SNS", 2700, 4096, 0, STM32_AIN(10)},
	/* Micro USB D- sense pin. Same scale as for D+. */
	[ADC_CH_USB_DN_SNS] = {"USB_DN_SNS", 2700, 4096, 0, STM32_AIN(11)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static void board_update_backlight(void)
{
	gpio_set_level(GPIO_BST_LED_EN, chipset_in_state(CHIPSET_STATE_ON) ||
		gpio_get_level(GPIO_LID_OPEN));
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_update_backlight, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_update_backlight, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_update_backlight, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_update_backlight, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_LID_CHANGE, board_update_backlight, HOOK_PRIO_DEFAULT);
