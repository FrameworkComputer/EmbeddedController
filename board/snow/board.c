/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Snow board-specific configuration */

#include "board.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "pmu_tpschrome.h"
#include "power_led.h"
#include "registers.h"
#include "spi.h"
#include "timer.h"
#include "util.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT (GPIO_OUTPUT | GPIO_OPEN_DRAIN)

#define INT_BOTH_FLOATING	(GPIO_INPUT | GPIO_INT_BOTH)
#define INT_BOTH_PULL_UP	(GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)

#define HARD_RESET_TIMEOUT_MS 5

/* GPIO interrupt handlers prototypes */
#ifndef CONFIG_TASK_GAIAPOWER
#define gaia_power_event NULL
#define gaia_suspend_event NULL
#define gaia_lid_event NULL
#else
void gaia_power_event(enum gpio_signal signal);
void gaia_suspend_event(enum gpio_signal signal);
void gaia_lid_event(enum gpio_signal signal);
#endif
#ifndef CONFIG_TASK_KEYSCAN
#define matrix_interrupt NULL
#endif

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_PWR_ON_L", GPIO_B, (1<<5),  GPIO_INT_BOTH, gaia_power_event},
	{"PP1800_LDO2", GPIO_A, (1<<1),  GPIO_INT_BOTH, gaia_power_event},
	{"XPSHOLD",     GPIO_A, (1<<3),  GPIO_INT_BOTH, gaia_power_event},
	{"CHARGER_INT", GPIO_C, (1<<4),  GPIO_INT_FALLING, pmu_irq_handler},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_RISING, gaia_lid_event},
	{"SUSPEND_L",   GPIO_A, (1<<7),  INT_BOTH_FLOATING, gaia_suspend_event},
	{"WP_L",        GPIO_B, (1<<4),  GPIO_INPUT, NULL},
	{"KB_IN00",     GPIO_C, (1<<8),  GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN01",     GPIO_C, (1<<9),  GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN02",     GPIO_C, (1<<10), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN03",     GPIO_C, (1<<11), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN04",     GPIO_C, (1<<12), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN05",     GPIO_C, (1<<14), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN06",     GPIO_C, (1<<15), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN07",     GPIO_D, (1<<2),  GPIO_KB_INPUT, matrix_interrupt},
	/* Other inputs */
	{"AC_PWRBTN_L", GPIO_A, (1<<0), GPIO_INT_BOTH, NULL},
	{"SPI1_NSS",    GPIO_A, (1<<4), GPIO_DEFAULT, NULL},
	/*
	 * I2C pins should be configured as inputs until I2C module is
	 * initialized. This will avoid driving the lines unintentionally.
	 */
	{"I2C1_SCL",    GPIO_B, (1<<6),  GPIO_INPUT, NULL},
	{"I2C1_SDA",    GPIO_B, (1<<7),  GPIO_INPUT, NULL},
	{"I2C2_SCL",    GPIO_B, (1<<10), GPIO_INPUT, NULL},
	{"I2C2_SDA",    GPIO_B, (1<<11), GPIO_INPUT, NULL},
	/* Outputs */
	{"AC_STATUS",   GPIO_A, (1<<5), GPIO_DEFAULT, NULL},
	{"SPI1_MISO",   GPIO_A, (1<<6), GPIO_DEFAULT, NULL},
	{"EN_PP1350",   GPIO_A, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EN_PP5000",   GPIO_A, (1<<11),  GPIO_OUT_LOW, NULL},
	{"EN_PP3300",   GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"PMIC_PWRON_L",GPIO_A, (1<<12), GPIO_OUT_HIGH, NULL},
	{"PMIC_RESET",  GPIO_A, (1<<15), GPIO_OUT_LOW, NULL},
	{"ENTERING_RW", GPIO_D, (1<<0),  GPIO_OUT_LOW, NULL},
	{"CHARGER_EN",  GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EC_INT",      GPIO_B, (1<<9),  GPIO_HI_Z, NULL},
	{"CODEC_INT",   GPIO_D, (1<<1),  GPIO_HI_Z, NULL},
	{"LED_POWER_L", GPIO_B, (1<<3),  GPIO_INPUT, NULL},
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

void configure_board(void)
{
	uint32_t val;

	dma_init();

	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32_RCC_APB2ENR |= 0x1fd;

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
	val = STM32_GPIO_CRH_OFF(GPIO_A) & ~0x00000ff0;
	val |= 0x00000890;
	STM32_GPIO_CRH_OFF(GPIO_A) = val;

	/* EC_INT is output, open-drain */
	val = STM32_GPIO_CRH_OFF(GPIO_B) & ~0xf0;
	val |= 0x50;
	STM32_GPIO_CRH_OFF(GPIO_B) = val;
	/* put GPIO in Hi-Z state */
	gpio_set_level(GPIO_EC_INT, 1);
}

/* GPIO configuration to be done after I2C module init */
void board_i2c_post_init(int port)
{
	uint32_t val;

	/* enable alt. function (open-drain) */
	if (port == STM32_I2C1_PORT) {
		/* I2C1 is on PB6-7 */
		val = STM32_GPIO_CRL_OFF(GPIO_B) & ~0xff000000;
		val |= 0xdd000000;
		STM32_GPIO_CRL_OFF(GPIO_B) = val;
	} else if (port == STM32_I2C2_PORT) {
		/* I2C2 is on PB10-11 */
		val = STM32_GPIO_CRH_OFF(GPIO_B) & ~0x0000ff00;
		val |= 0x0000dd00;
		STM32_GPIO_CRH_OFF(GPIO_B) = val;
	}
}

void configure_board_late(void)
{
#ifdef CONFIG_AC_POWER_STATUS
	gpio_set_flags(GPIO_AC_STATUS, GPIO_OUT_HIGH);
#endif
}

void board_interrupt_host(int active)
{
	/* interrupt host by using active low EC_INT signal */
	gpio_set_level(GPIO_EC_INT, !active);
}

void board_keyboard_suppress_noise(void)
{
	/* notify audio codec of keypress for noise suppression */
	gpio_set_level(GPIO_CODEC_INT, 0);
	gpio_set_level(GPIO_CODEC_INT, 1);
}

void board_power_led_config(enum powerled_config config)
{
	uint32_t val;

	switch (config) {
	case POWERLED_CONFIG_PWM:
		val = STM32_GPIO_CRL_OFF(GPIO_B) & ~0x0000f000;
		val |= 0x00009000;	/* alt. function (TIM2/PWM) */
		STM32_GPIO_CRL_OFF(GPIO_B) = val;
		break;
	case POWERLED_CONFIG_MANUAL_OFF:
		/*
		 * Re-configure GPIO as a floating input. Alternatively we could
		 * configure it as an open-drain output and set it to high
		 * impedence, but reconfiguring as an input had better results
		 * in testing.
		 */
		gpio_set_flags(GPIO_LED_POWER_L, GPIO_INPUT);
		gpio_set_level(GPIO_LED_POWER_L, 1);
		break;
	case POWERLED_CONFIG_MANUAL_ON:
		gpio_set_flags(GPIO_LED_POWER_L, GPIO_OUTPUT | GPIO_OPEN_DRAIN);
		gpio_set_level(GPIO_LED_POWER_L, 0);
		break;
	default:
		break;
	}
}

enum {
	/* Time between requesting bus and deciding that we have it */
	BUS_SLEW_DELAY_US	= 10,

	/* Time between retrying to see if the AP has released the bus */
	BUS_WAIT_RETRY_US	= 3000,

	/* Time to wait until the bus becomes free */
	BUS_WAIT_FREE_US	= 100 * 1000,
};

/*
 * This reflects the desired value of GPIO_EC_CLAIM to ensure that the
 * GPIO is driven correctly when re-enabled before AP power on.
 */
static char i2c_claimed_by_ec;

static int board_pre_init_hook(void)
{
#ifdef CONFIG_ARBITRATE_I2C
	gpio_set_flags(GPIO_AP_CLAIM, GPIO_PULL_UP);
	gpio_set_level(GPIO_EC_CLAIM, i2c_claimed_by_ec ? 0 : 1);
	gpio_set_flags(GPIO_EC_CLAIM, GPIO_OUTPUT);
	usleep(BUS_SLEW_DELAY_US);
#endif
	return 0;
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_pre_init_hook, HOOK_PRIO_DEFAULT);

static int board_startup_hook(void)
{
	gpio_set_flags(GPIO_SUSPEND_L, INT_BOTH_PULL_UP);
	return 0;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_startup_hook, HOOK_PRIO_DEFAULT);

static int board_shutdown_hook(void)
{
	/* Disable pull-up on SUSPEND_L during shutdown to prevent leakage */
	gpio_set_flags(GPIO_SUSPEND_L, INT_BOTH_FLOATING);

#ifdef CONFIG_ARBITRATE_I2C
	gpio_set_flags(GPIO_AP_CLAIM, GPIO_INPUT);
	gpio_set_flags(GPIO_EC_CLAIM, GPIO_INPUT);
#endif
	return 0;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_shutdown_hook, HOOK_PRIO_DEFAULT);


#ifdef CONFIG_ARBITRATE_I2C

int board_i2c_claim(int port)
{
	timestamp_t start;

	if (port != I2C_PORT_HOST)
		return EC_SUCCESS;

	/* If AP is off, we have the bus */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		i2c_claimed_by_ec = 1;
		return EC_SUCCESS;
	}

	/* Start a round of trying to claim the bus */
	start = get_time();
	do {
		timestamp_t start_retry;
		int waiting = 0;

		/* Indicate that we want to claim the bus */
		gpio_set_level(GPIO_EC_CLAIM, 0);
		usleep(BUS_SLEW_DELAY_US);

		/* Wait for the AP to release it */
		start_retry = get_time();
		while (time_since32(start_retry) < BUS_WAIT_RETRY_US) {
			if (gpio_get_level(GPIO_AP_CLAIM)) {
				/* We got it, so return */
				i2c_claimed_by_ec = 1;
				return EC_SUCCESS;
			}

			if (!waiting)
				waiting = 1;
		}

		/* It didn't release, so give up, wait, and try again */
		gpio_set_level(GPIO_EC_CLAIM, 1);

		usleep(BUS_WAIT_RETRY_US);
	} while (time_since32(start) < BUS_WAIT_FREE_US);

	gpio_set_level(GPIO_EC_CLAIM, 1);
	usleep(BUS_SLEW_DELAY_US);
	i2c_claimed_by_ec = 0;

	panic_puts("Unable to access I2C bus (arbitration timeout)\n");
	return EC_ERROR_BUSY;
}

void board_i2c_release(int port)
{
	if (port == I2C_PORT_HOST) {
		/* Release our claim */
		gpio_set_level(GPIO_EC_CLAIM, 1);
		usleep(BUS_SLEW_DELAY_US);
		i2c_claimed_by_ec = 0;
	}
}
#endif /* CONFIG_ARBITRATE_I2C */

/*
 * Force the pmic to reset completely.  This forces an entire system reset,
 * and therefore should never return
 */
void board_hard_reset(void)
{
	/* Force a hard reset of tps Chrome */
	gpio_set_level(GPIO_PMIC_RESET, 1);

	/* Delay while the power is cut */
	udelay(HARD_RESET_TIMEOUT_MS * 1000);

	/* Shouldn't get here unless the board doesn't have this capability */
	panic_puts("Hard reset failed! (this board may not be capable)\n");
}

#ifdef CONFIG_PMU_BOARD_INIT

/**
 * Initialize PMU register settings
 *
 * PMU init settings depend on board configuration. This function should be
 * called inside PMU init function.
 */
int board_pmu_init(void)
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
#endif /* CONFIG_BOARD_PMU_INIT */
