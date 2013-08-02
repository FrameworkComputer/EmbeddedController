/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* McCroskey board-specific configuration */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT (GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_OUT_LOW)

#define HARD_RESET_TIMEOUT_MS 5

static void kbd_power_on(enum gpio_signal signal);

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_IN00",        GPIO_B, (1<<8),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN01",        GPIO_B, (1<<9),  GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN02",        GPIO_B, (1<<10), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN03",        GPIO_B, (1<<11), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN04",        GPIO_B, (1<<12), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN05",        GPIO_B, (1<<13), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN06",        GPIO_B, (1<<14), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KB_IN07",        GPIO_B, (1<<15), GPIO_KB_INPUT,
	 keyboard_raw_gpio_interrupt},
	{"KBD_PWR_BUTTON", GPIO_B, (1<<2),  GPIO_INPUT, kbd_power_on},

	{"OMZO_RDY_L",     GPIO_A, (1<<0),  GPIO_INPUT, NULL},	/* PA0_WKUP */
	{"OZMO_RST_L",     GPIO_A, (1<<2),  GPIO_ODR_HIGH, NULL},
	{"VBUS_UP_DET",    GPIO_A, (1<<3),  GPIO_INPUT, NULL},
	{"OZMO_REQ_L",     GPIO_A, (1<<8),  GPIO_INPUT, NULL},
	{"CHARGE_ZERO",    GPIO_B, (1<<0),  GPIO_INPUT, NULL},
	{"CHARGE_SHUNT",   GPIO_B, (1<<1),  GPIO_INPUT, NULL},
	{"PMIC_INT_L",     GPIO_B, (1<<5),  GPIO_INPUT, NULL},

	/*
	 * I2C pins should be configured as inputs until I2C module is
	 * initialized. This will avoid driving the lines unintentionally.
	 */
	{"I2C1_SCL",       GPIO_B, (1<<6),  GPIO_INPUT, NULL},
	{"I2C1_SDA",       GPIO_B, (1<<7),  GPIO_INPUT, NULL},

	{"KB_OUT00",       GPIO_C, (1<<0),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT01",       GPIO_C, (1<<1),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT02",       GPIO_C, (1<<2),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT03",       GPIO_C, (1<<3),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT04",       GPIO_C, (1<<4),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT05",       GPIO_C, (1<<5),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT06",       GPIO_C, (1<<6),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT07",       GPIO_C, (1<<7),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT08",       GPIO_C, (1<<8),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT09",       GPIO_C, (1<<9),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT10",       GPIO_C, (1<<10), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT11",       GPIO_C, (1<<11), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT12",       GPIO_C, (1<<12), GPIO_KB_OUTPUT, NULL},
	{"USB_VBUS_CTRL",  GPIO_C, (1<<13), GPIO_OUT_LOW, NULL},
	{"HUB_RESET",      GPIO_C, (1<<14), GPIO_ODR_HIGH, NULL},
	{"WP_L",           GPIO_D, (1<<2),  GPIO_INPUT, NULL},

	/* FIXME: make this alt. function */
	{"BL_PWM",         GPIO_A, (1<<1),  GPIO_OUTPUT, NULL},

	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("EC_INT"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),

#if 0
	/* Other GPIOs (probably need to be set up below as alt. function) */
	{"STM_USBDM",      GPIO_A, (1<<11), GPIO_DEFAULT, NULL},
	{"STM_USBDP",      GPIO_A, (1<<12), GPIO_DEFAULT, NULL},
	{"JTMS_SWDIO",     GPIO_A, (1<<13), GPIO_DEFAULT, NULL},
	{"JTCK_SWCLK",     GPIO_A, (1<<14), GPIO_DEFAULT, NULL},
	{"JTDI",           GPIO_A, (1<<15), GPIO_DEFAULT, NULL},
	{"JTDO",           GPIO_B, (1<<3),  GPIO_DEFAULT, NULL},
	{"JNTRST",         GPIO_B, (1<<4),  GPIO_DEFAULT, NULL},
	{"OSC32_OUT",      GPIO_C, (1<<15), GPIO_DEFAULT, NULL},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	/*
	 * TODO(rspangler): use this instead of hard-coded register writes in
	 * board_config_pre_init().
	 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

void board_config_pre_init(void)
{
	uint32_t val;

	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32_RCC_APB2ENR |= 0x1fd;

#ifdef CONFIG_SPI
	/* SPI1 on pins PA4-7 (alt. function push-pull, 10MHz) */
	/* FIXME: Connected device SPI freq is fxo/2 in master mode, fxo/4
	 * in slave mode. fxo ranges from 12-40MHz */
	val = STM32_GPIO_CRL(GPIO_A) & ~0xffff0000;
	val |= 0x99990000;
	STM32_GPIO_CRL(GPIO_A) = val;
#endif

	/* remap OSC_IN/OSC_OUT to PD0/PD1 */
	STM32_GPIO_AFIO_MAPR |= 1 << 15;

	/* use PB3 as a GPIO, so disable JTAG and keep only SWD */
	STM32_GPIO_AFIO_MAPR = (STM32_GPIO_AFIO_MAPR & ~(0x7 << 24))
			       | (2 << 24);

	/* remap TIM2_CH2 to PB3 */
	STM32_GPIO_AFIO_MAPR = (STM32_GPIO_AFIO_MAPR & ~(0x3 << 8))
			       | (1 << 8);

	/*
	 * Set alternate function for USART1. For alt. function input
	 * the port is configured in either floating or pull-up/down
	 * input mode (ref. section 7.1.4 in datasheet RM0041):
	 * PA9:  Tx, alt. function output
	 * PA10: Rx, input with pull-down
	 *
	 * note: see crosbug.com/p/12223 for more info
	 */
	val = STM32_GPIO_CRH(GPIO_A) & ~0x00000ff0;
	val |= 0x00000890;
	STM32_GPIO_CRH(GPIO_A) = val;
}

/* GPIO configuration to be done after I2C module init */
void board_i2c_post_init(int port)
{
	uint32_t val;

	/* enable alt. function (open-drain) */
	if (port == STM32_I2C1_PORT) {
		/* I2C1 is on PB6-7 */
		val = STM32_GPIO_CRL(GPIO_B) & ~0xff000000;
		val |= 0xdd000000;
		STM32_GPIO_CRL(GPIO_B) = val;
	}
}

void chipset_reset(int is_cold)
{
	/* FIXME: this is just a stub for now... */
}

void kbd_power_on(enum gpio_signal signal)
{
	/* FIXME: this is just a stub for now... */
}
