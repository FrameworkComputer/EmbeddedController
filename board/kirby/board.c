/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Pit board-specific configuration */

#include "battery_pack.h"
#include "common.h"
#include "extpower.h"
#include "gaia_power.h"
#include "gpio.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_PWR_ON_L",   GPIO_A, (1<<0),  GPIO_INT_BOTH, gaia_power_event},
	{"XPSHOLD",       GPIO_E, (1<<5),  GPIO_INT_RISING, gaia_power_event},
	{"CHARGER_INT_L", GPIO_E, (1<<2),  GPIO_INT_FALLING,
	 NULL /*pmu_irq_handler*/},
	{"USB_CHG_INT",   GPIO_E, (1<<15), GPIO_INT_RISING,
	 NULL /*extpower_interrupt*/},
	{"USBPD_INT_L",   GPIO_E, (1<<1),  GPIO_INT_FALLING,
	 NULL /*extpower_interrupt*/},
	{"LID_OPEN",      GPIO_E, (1<<6),  GPIO_INT_BOTH, lid_interrupt},
	{"SUSPEND_L",     GPIO_E, (1<<3),  GPIO_INT_BOTH, gaia_suspend_event},
	{"SPI1_NSS",      GPIO_A, (1<<4),  GPIO_INT_BOTH | GPIO_PULL_UP,
	 spi_event},
	{"AC_PRESENT",    GPIO_A, (1<<1),  GPIO_INT_BOTH,
	 NULL /*extpower_interrupt*/},
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
	{"ID_MUX",        GPIO_B, (1<<11), GPIO_ODR_HIGH, NULL},
	{"BCHGR_OTG",     GPIO_B, (1<<12), GPIO_OUT_LOW, NULL},
	{"BCHGR_PSEL",    GPIO_B, (1<<14), GPIO_OUT_HIGH, NULL},
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
	 * Unimplemented
	 * TODO: Remove these when codes compile without them
	 */
	GPIO_SIGNAL_NOT_IMPLEMENTED("I2C2_SCL"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("I2C2_SDA"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Battery temperature ranges in degrees C */
const struct battery_temperature_ranges bat_temp_ranges = {
	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c       = 0,
	.charging_max_c       = 60,
	.discharging_min_c    = 0,
	.discharging_max_c    = 100,
};

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"host", I2C_PORT_HOST, 100},
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ports) == I2C_PORTS_USED);

void board_config_post_gpio_init(void)
{
	/* I2C SCL/SDA on PB6-7 */
	gpio_set_alternate_function(GPIO_B, (1 << 7) | (1 << 6), GPIO_ALT_I2C);

	/* USART1 on pins PA9/PA10 */
	gpio_set_alternate_function(GPIO_A, (1 << 9) | (1 << 10),
				    GPIO_ALT_USART);

	/* TODO: Set TIM3 for PC6-8 here? */
}
