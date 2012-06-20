/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Snow board-specific configuration */

#include "board.h"
#include "common.h"
#include "dma.h"
#include "gpio.h"
#include "i2c.h"
#include "registers.h"
#include "spi.h"
#include "util.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT (GPIO_OUTPUT | GPIO_OPEN_DRAIN)

/* GPIO interrupt handlers prototypes */
#ifndef CONFIG_TASK_GAIAPOWER
#define gaia_power_event NULL
#define gaia_suspend_event NULL
#else
void gaia_power_event(enum gpio_signal signal);
void gaia_suspend_event(enum gpio_signal signal);
#endif
#ifndef CONFIG_TASK_KEYSCAN
#define matrix_interrupt NULL
#endif

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_PWR_ON_L", GPIO_B, (1<<5),  GPIO_INT_BOTH, gaia_power_event},
	{"PP1800_LDO2", GPIO_A, (1<<1),  GPIO_INT_BOTH, gaia_power_event},
	{"XPSHOLD",     GPIO_A, (1<<3),  GPIO_INT_RISING, gaia_power_event},
	{"CHARGER_INT", GPIO_C, (1<<4),  GPIO_INT_RISING, NULL},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_BOTH, NULL},
	{"SUSPEND_L",   GPIO_A, (1<<7),  GPIO_INT_BOTH, gaia_suspend_event},
	{"KB_IN00",     GPIO_C, (1<<8),  GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN01",     GPIO_C, (1<<9),  GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN02",     GPIO_C, (1<<10), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN03",     GPIO_C, (1<<11), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN04",     GPIO_C, (1<<12), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN05",     GPIO_C, (1<<14), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN06",     GPIO_C, (1<<15), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN07",     GPIO_D, (1<<2),  GPIO_KB_INPUT, matrix_interrupt},
	/* Other inputs */
	{"SPI1_NSS",    GPIO_A, (1<<4), GPIO_INT_RISING, NULL},

	/* Outputs */
	{"EN_PP1350",   GPIO_A, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EN_PP5000",   GPIO_A, (1<<11),  GPIO_OUT_LOW, NULL},
	{"EN_PP3300",   GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"PMIC_PWRON_L",GPIO_A, (1<<12), GPIO_OUT_HIGH, NULL},
	{"ENTERING_RW", GPIO_D, (1<<0),  GPIO_OUT_LOW, NULL},
	{"CHARGER_EN",  GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},
	{"POWER_LED_L", GPIO_B, (1<<3),  GPIO_OUT_HIGH, NULL},
	{"EC_INT",      GPIO_B, (1<<9),  GPIO_HI_Z, NULL},
	{"CODEC_INT",   GPIO_D, (1<<1),  GPIO_HI_Z, NULL},
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
};

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

void configure_board(void)
{
	uint32_t val;

	dma_init();

	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32_RCC_APB2ENR |= 0x1fd;

	/* Enable SPI */
	STM32_RCC_APB2ENR |= (1<<12);

	/* remap OSC_IN/OSC_OUT to PD0/PD1 */
	STM32_GPIO_AFIO_MAPR |= 1 << 15;

	/* use PB3 as a GPIO, so disable JTAG and keep only SWD */
	STM32_GPIO_AFIO_MAPR = (STM32_GPIO_AFIO_MAPR & ~(0x7 << 24))
			       | (2 << 24);

	/*
	 * I2C SCL/SDA on PB10-11 and PB6-7, bi-directional, no pull-up/down,
	 * initialized as hi-Z until alt. function is set
	 */
	val = STM32_GPIO_CRH_OFF(GPIO_B) & ~0x0000ff00;
	val |= 0x0000dd00;
	STM32_GPIO_CRH_OFF(GPIO_B) = val;

	val = STM32_GPIO_CRL_OFF(GPIO_B) & ~0xff000000;
	val |= 0xdd000000;
	STM32_GPIO_CRL_OFF(GPIO_B) = val;

	STM32_GPIO_BSRR_OFF(GPIO_B) |= (1<<11) | (1<<10) | (1<<7) | (1<<6);

	/* Select Alternate function for USART1 on pins PA9/PA10 */
	val = STM32_GPIO_CRH_OFF(GPIO_A) & ~0x00000ff0;
	val |= 0x00000990;
	STM32_GPIO_CRH_OFF(GPIO_A) = val;

	/* EC_INT is output, open-drain */
	val = STM32_GPIO_CRH_OFF(GPIO_B) & ~0xf0;
	val |= 0x50;
	STM32_GPIO_CRH_OFF(GPIO_B) = val;
	/* put GPIO in Hi-Z state */
	gpio_set_level(GPIO_EC_INT, 1);
}

void board_interrupt_host(int active)
{
	/* interrupt host by using active low EC_INT signal */
	gpio_set_level(GPIO_EC_INT, !active);
}
