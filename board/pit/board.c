/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Pit board-specific configuration */

#include "common.h"
#include "extpower.h"
#include "gaia_power.h"
#include "gpio.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "pmu_tpschrome.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT GPIO_ODR_HIGH

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_PWR_ON_L", GPIO_B, (1<<5),  GPIO_INT_BOTH, gaia_power_event},
	{"PP1800_LDO2", GPIO_A, (1<<1),  GPIO_INT_BOTH, gaia_power_event},
	{"XPSHOLD",     GPIO_A, (1<<3),  GPIO_INT_RISING, gaia_power_event},
	{"CHARGER_INT", GPIO_C, (1<<6),  GPIO_INT_RISING, pmu_irq_handler},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_BOTH, lid_interrupt},
	{"SUSPEND_L",   GPIO_C, (1<<7),  GPIO_INT_BOTH, gaia_suspend_event},
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
	{"EN_PP1350",   GPIO_H, (1<<1),  GPIO_OUT_LOW, NULL},
	{"EN_PP3300",   GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"EN_PP5000",   GPIO_A, (1<<11), GPIO_OUT_LOW, NULL},
	{"ENTERING_RW", GPIO_H, (1<<0),  GPIO_OUT_LOW, NULL},
	{"I2C1_SCL",    GPIO_B, (1<<6),  GPIO_ODR_HIGH, NULL},
	{"I2C1_SDA",    GPIO_B, (1<<7),  GPIO_ODR_HIGH, NULL},
	{"I2C2_SCL",    GPIO_B, (1<<10), GPIO_ODR_HIGH, NULL},
	{"I2C2_SDA",    GPIO_B, (1<<11), GPIO_ODR_HIGH, NULL},
	{"LED_POWER_L", GPIO_A, (1<<2), GPIO_OUT_HIGH, NULL},
	{"PMIC_PWRON",  GPIO_A, (1<<12), GPIO_OUT_LOW, NULL},
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
	{"KB_OUT12",    GPIO_A, (1<<13),  GPIO_KB_OUTPUT, NULL},
};

/* I2C ports */
const struct i2c_port_t i2c_ports[I2C_PORTS_USED] = {
	{"host", I2C_PORT_HOST, 100},
};

void board_config_post_gpio_init(void)
{
	/* I2C SCL/SDA on PB10-11 and PB6-7 */
	gpio_set_alternate_function(GPIO_B,
				    (1 << 11) | (1 << 10) | (1 << 7) | (1 << 6),
				    GPIO_ALT_I2C);

	/* USART1 on pins PA9/PA10 */
	gpio_set_alternate_function(GPIO_A, (1 << 9) | (1 << 10),
				    GPIO_ALT_USART);

	/* TIM2_CH2 on PB3 */
	gpio_set_alternate_function(GPIO_B, (1 << 3), GPIO_ALT_TIM2);
}

int pmu_board_init(void)
{
	int ver, failure = 0;

	/* Set fast charging timeout to 6 hours*/
	if (!failure)
		failure = pmu_set_fastcharge(TIMEOUT_6HRS);
	/* Enable external gpio CHARGER_EN control */
	if (!failure)
		failure = pmu_enable_ext_control(1);
	/* Disable force charging */
	if (!failure)
		failure = pmu_enable_charger(0);

	/* Set NOITERM bit */
	if (!failure)
		failure = pmu_low_current_charging(1);

	/*
	 * High temperature charging
	 *   termination voltage: 2.1V
	 *   termination current: 100%
	 */
	if (!failure)
		failure = pmu_set_term_voltage(RANGE_T34, TERM_V2100);
	if (!failure)
		failure = pmu_set_term_current(RANGE_T34, TERM_I1000);
	/*
	 * Standard temperature charging
	 *   termination voltage: 2.1V
	 *   termination current: 100%
	 */
	if (!failure)
		failure = pmu_set_term_voltage(RANGE_T23, TERM_V2100);
	if (!failure)
		failure = pmu_set_term_current(RANGE_T23, TERM_I1000);

	/*
	 * Ignore TPSCHROME NTC reading in T40. This is snow board specific
	 * setting. Check:
	 *   http://crosbug.com/p/12221
	 *   http://crosbug.com/p/13171
	 */
	if (!failure)
		failure = pmu_set_term_voltage(RANGE_T40, TERM_V2100);
	if (!failure)
		failure = pmu_set_term_current(RANGE_T40, TERM_I1000);

	/* Workaround init values before ES3 */
	if (pmu_version(&ver) || ver < 3) {
		/* Termination current: 75% */
		if (!failure)
			failure = pmu_set_term_current(RANGE_T34, TERM_I0750);
		if (!failure)
			failure = pmu_set_term_current(RANGE_T23, TERM_I0750);
		if (!failure)
			failure = pmu_set_term_current(RANGE_T40, TERM_I0750);
	}

	return failure ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}
