/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Daisy board-specific configuration */

#include "common.h"
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

/*
 * Daisy keyboard summary:
 * 1. KEYSCAN task woken up via GPIO external interrupt when a key is pressed.
 * 2. The task scans the keyboard matrix for changes. If key state has
 *    changed, the board-specific kb_send() function is called.
 * 3. For Daisy, the EC is connected via I2C and acts as a slave, so the AP
 *    must initiate all transactions. EC_INT is driven low to interrupt AP when
 *    new data becomes available.
 * 4. When the AP is interrupted, it initiates two i2c transactions:
 *    1. 1-byte write: AP writes 0x01 to make EC send keyboard state.
 *    2. 14-byte read: AP reads 1 keyboard packet (13 byte keyboard state +
 *       1-byte checksum).
 */

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT (GPIO_OUTPUT | GPIO_PULL_UP | GPIO_OPEN_DRAIN)

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_PWR_ON_L", GPIO_B, (1<<5),  GPIO_INT_BOTH, gaia_power_event},
	{"PP1800_LDO2", GPIO_A, (1<<1),  GPIO_INT_BOTH, gaia_power_event},
	{"XPSHOLD",     GPIO_A, (1<<3),  GPIO_INT_RISING, gaia_power_event},
	{"CHARGER_INT", GPIO_C, (1<<4),  GPIO_INT_RISING, NULL},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_BOTH, lid_interrupt},
	{"SUSPEND_L",   GPIO_A, (1<<7),  GPIO_INT_BOTH, gaia_suspend_event},
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
	{"AC_PWRBTN_L", GPIO_A, (1<<0), GPIO_INT_BOTH, NULL},
	{"SPI1_NSS",    GPIO_A, (1<<4), GPIO_PULL_UP, NULL},
	/*
	 * I2C pins should be configured as inputs until I2C module is
	 * initialized. This will avoid driving the lines unintentionally.
	 */
	{"I2C1_SCL",    GPIO_B, (1<<6),  GPIO_INPUT, NULL},
	{"I2C1_SDA",    GPIO_B, (1<<7),  GPIO_INPUT, NULL},
	{"I2C2_SCL",    GPIO_B, (1<<10), GPIO_INPUT, NULL},
	{"I2C2_SDA",    GPIO_B, (1<<11), GPIO_INPUT, NULL},
	/* Outputs */
	{"AC_STATUS",   GPIO_A, (1<<5), GPIO_OUT_HIGH, NULL},
	{"SPI1_MISO",   GPIO_A, (1<<6), GPIO_OUT_HIGH, NULL},
	{"EN_PP1350",   GPIO_A, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EN_PP5000",   GPIO_A, (1<<11),  GPIO_OUT_LOW, NULL},
	{"EN_PP3300",   GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"PMIC_PWRON_L", GPIO_A, (1<<12), GPIO_OUT_HIGH, NULL},
	{"ENTERING_RW", GPIO_H, (1<<0),  GPIO_OUT_LOW, NULL},
	{"CHARGER_EN",  GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},
	{"POWER_LED_L", GPIO_B, (1<<3),  GPIO_OUT_HIGH, NULL},
	{"EC_INT",      GPIO_B, (1<<9),  GPIO_HI_Z, NULL},
	{"CODEC_INT",   GPIO_H, (1<<1),  GPIO_HI_Z, NULL},
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
	{"KB_OUT11",    GPIO_C, (1<<6),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT12",    GPIO_C, (1<<7),  GPIO_KB_OUTPUT, NULL},

	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("WP_L"),
};

/* I2C ports */
const struct i2c_port_t i2c_ports[I2C_PORTS_USED] = {
	{"0", 0, 100},
	{"1", 1, 100},
};

/* Auto detect I2C host port
 * Daisy board has two I2C ports, I2C1(0) and I2C2(1), that can be configured
 * as host. PMU chip is connected directly to the EC, and hence can be used
 * for port detection
 */
#ifdef CONFIG_I2C_HOST_AUTO
static int i2c_host_port = -1;

/* Detect if tps65090 pmu is present on a i2c bus.
 * This hack makes one single ec binary to work on boards with different
 * stuffing options.
 *
 * TODO: Revert i2c host port detection after all dev boards been reworked or
 * deprecated. Issue: http://crosbug.com/p/10622
 */
static int tps65090_is_present(int bus)
{
	const int tps65090_addr = 0x90;
	const int charger_ctrl_offset0 = 4;
	int rv, reg;

	rv = i2c_read8(bus, tps65090_addr, charger_ctrl_offset0, &reg);

	if (rv == EC_SUCCESS)
		return 1;
	return 0;
}

int board_i2c_host_port(void)
{
	/* Default I2C host configuration is I2C1(0).
	 * If PMU doesn't ack on I2C2(1), set the host port to 0.
	 */
	if (i2c_host_port == -1)
		i2c_host_port = tps65090_is_present(1) ? 1 : 0;

	return i2c_host_port;
}
#endif /* CONFIG_I2C_HOST_AUTO */

void board_config_post_gpio_init(void)
{
	/* I2C SCL/SDA on PB10-11 and PB6-7 */
	gpio_set_alternate_function(GPIO_B, (1<<11) |
					(1<<10) |
					(1<<7)  |
					(1<<6), GPIO_ALT_I2C);

	/* Select Alternate function for USART1 on pins PA9/PA10 */
	gpio_set_alternate_function(GPIO_A, (1<<9) | (1<<10), GPIO_ALT_USART);
}

void keyboard_suppress_noise(void)
{
	/* notify audio codec of keypress for noise suppression */
	gpio_set_level(GPIO_CODEC_INT, 0);
	gpio_set_level(GPIO_CODEC_INT, 1);
}
