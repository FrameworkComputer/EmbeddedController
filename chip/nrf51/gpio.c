/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/*
 * For each interrupt (INT0-INT3, PORT), record which GPIO entry uses it.
 */

static const struct gpio_info *gpio_ints[NRF51_GPIOTE_IN_COUNT];
static const struct gpio_info *gpio_int_port;

volatile uint32_t * const nrf51_alt_funcs[] = {
	/* UART */
	&NRF51_UART_PSELRTS,
	&NRF51_UART_PSELTXD,
	&NRF51_UART_PSELCTS,
	&NRF51_UART_PSELRXD,
	/* SPI1 (SPI Master) */
	&NRF51_SPI0_PSELSCK,
	&NRF51_SPI0_PSELMOSI,
	&NRF51_SPI0_PSELMISO,
	/* TWI0 (I2C) */
	&NRF51_TWI0_PSELSCL,
	&NRF51_TWI0_PSELSDA,
	/* SPI1 (SPI Master) */
	&NRF51_SPI1_PSELSCK,
	&NRF51_SPI1_PSELMOSI,
	&NRF51_SPI1_PSELMISO,
	/* TWI1 (I2C) */
	&NRF51_TWI1_PSELSCL,
	&NRF51_TWI1_PSELSDA,
	/* SPIS1 (SPI SLAVE) */
	&NRF51_SPIS1_PSELSCK,
	&NRF51_SPIS1_PSELMISO,
	&NRF51_SPIS1_PSELMOSI,
	&NRF51_SPIS1_PSELCSN,
	/* QDEC (ROTARY DECODER) */
	&NRF51_QDEC_PSELLED,
	&NRF51_QDEC_PSELA,
	&NRF51_QDEC_PSELB,
	/* LPCOMP (Low Power Comparator) */
	&NRF51_LPCOMP_PSEL,
};

const unsigned int nrf51_alt_func_count = ARRAY_SIZE(nrf51_alt_funcs);

/* Make sure the function table and defines stay in sync */
BUILD_ASSERT(NRF51_MAX_ALT_FUNCS == ARRAY_SIZE(nrf51_alt_funcs));

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	uint32_t val = 0;
	uint32_t bit = 31 - __builtin_clz(mask);

	if (flags & GPIO_OUTPUT)
		val |= NRF51_PIN_CNF_DIR_OUTPUT;
	else if (flags & GPIO_INPUT)
		val |= NRF51_PIN_CNF_DIR_INPUT;

	if (flags & GPIO_PULL_DOWN)
		val |= NRF51_PIN_CNF_PULLDOWN;
	else if (flags & GPIO_PULL_UP)
		val |= NRF51_PIN_CNF_PULLUP;

	/* TODO: Drive strength? H0D1? */
	if (flags & GPIO_OPEN_DRAIN)
		val |= NRF51_PIN_CNF_DRIVE_S0D1;

	if (flags & GPIO_OUTPUT) {
		if (flags & GPIO_HIGH)
			NRF51_GPIO0_OUTSET = mask;
		else if (flags & GPIO_LOW)
			NRF51_GPIO0_OUTCLR = mask;
	}

	/* Interrupt levels */
	if (flags & GPIO_INT_SHARED) {
		/*
		 * There are no shared edge-triggered interrupts;
		 * they're either high or low.
		 */
		ASSERT((flags & (GPIO_INT_F_RISING | GPIO_INT_F_FALLING)) == 0);
		ASSERT((flags & GPIO_INT_LEVEL) != GPIO_INT_LEVEL);
		if (flags & GPIO_INT_F_LOW)
			val |= NRF51_PIN_CNF_SENSE_LOW;
		else if (flags & GPIO_INT_F_HIGH)
			val |= NRF51_PIN_CNF_SENSE_HIGH;
	}

	NRF51_PIN_CNF(bit) = val;
}


static void gpio_init(void)
{
	task_enable_irq(NRF51_PERID_GPIOTE);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);


test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return !!(NRF51_GPIO0_IN & gpio_list[signal].mask);
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	if (value)
		NRF51_GPIO0_OUTSET = gpio_list[signal].mask;
	else
		NRF51_GPIO0_OUTCLR = gpio_list[signal].mask;
}


void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int is_warm = 0;
	int i;

	if (NRF51_POWER_RESETREAS & (1 << 2)) {
		/* This is a warm reboot */
		is_warm = 1;
	}

	/* Initialize Interrupt configuration */
	for (i = 0; i < NRF51_GPIOTE_IN_COUNT; i++)
		gpio_ints[i] = NULL;
	gpio_int_port = NULL;

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		int flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		/*
		 * If this is a warm reboot, don't set the output levels or
		 * we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}

/*
 * NRF51 doesn't have an alternate function table.
 * Use the pin select registers in place of the function number.
 */
void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	uint32_t bit = 31 - __builtin_clz(mask);

	ASSERT((~mask & (1 << bit)) == 0); /* Only one bit set. */
	ASSERT(port == GPIO_0);
	ASSERT((func >= 0 && func < nrf51_alt_func_count) || func == -1);

	/* Remove the previous setting(s) */
	if (func == -1) {
		int i;
		for (i = 0; i < nrf51_alt_func_count; i++) {
			if (*(nrf51_alt_funcs[i]) == bit)
				*(nrf51_alt_funcs[i]) = 0xffffffff;
		}
	} else {
		*(nrf51_alt_funcs[func]) = bit;
	}
}


/*
 *  Enable the interrupt associated with the "signal"
 *  The architecture has one general (PORT)
 *  and NRF51_GPIOTE_IN_COUNT single-pin (IN0, IN1, ...) interrupts.
 *
 */
int gpio_enable_interrupt(enum gpio_signal signal)
{
	int pin;
	const struct gpio_info *g = gpio_list + signal;

	/* Fail if not implemented or no interrupt handler */
	if (!g->mask || !g->irq_handler)
		return EC_ERROR_INVAL;

	/* If it's not shared, use INT0-INT3, otherwise use PORT. */
	if (!(g->flags & GPIO_INT_SHARED)) {
		int int_num, free_slot = -1;
		uint32_t event_config = 0;

		for (int_num = 0; int_num < NRF51_GPIOTE_IN_COUNT; int_num++) {
			if (gpio_ints[int_num] == g)
				return EC_SUCCESS; /* This is already set up. */

			if (gpio_ints[int_num] == NULL && free_slot == -1)
				free_slot = int_num;
		}

		ASSERT(free_slot != -1);

		gpio_ints[free_slot] = g;
		pin = 31 - __builtin_clz(g->mask);
		event_config = (pin << NRF51_GPIOTE_PSEL_POS) |
			NRF51_GPIOTE_MODE_EVENT;

		ASSERT(g->flags & (GPIO_INT_F_RISING | GPIO_INT_F_FALLING));

		/* RISING | FALLING = TOGGLE */
		if (g->flags & GPIO_INT_F_RISING)
			event_config |= NRF51_GPIOTE_POLARITY_LOTOHI;
		if (g->flags & GPIO_INT_F_FALLING)
			event_config |= NRF51_GPIOTE_POLARITY_HITOLO;

		NRF51_GPIOTE_CONFIG(free_slot) = event_config;

		/* Enable the IN[] interrupt. */
		NRF51_GPIOTE_INTENSET = 1 << free_slot;

	} else {
		/* The first handler for the shared interrupt wins. */
		if (gpio_int_port == NULL) {
			gpio_int_port = g;

			/* Enable the PORT interrupt. */
			NRF51_GPIOTE_INTENSET = 1 << NRF51_GPIOTE_PORT_BIT;
		}
	}

	return EC_SUCCESS;
}

/*
 *  Disable the interrupt associated with the "signal"
 *  The architecture has one general (PORT)
 *  and NRF51_GPIOTE_IN_COUNT single-pin (IN0, IN1, ...) interrupts.
 */
int gpio_disable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	int i;

	/* Fail if not implemented or no interrupt handler */
	if (!g->mask || !g->irq_handler)
		return EC_ERROR_INVAL;

	/* If it's not shared, use INT0-INT3, otherwise use PORT. */
	if (!(g->flags && GPIO_INT_SHARED)) {
		for (i = 0; i < NRF51_GPIOTE_IN_COUNT; i++) {
			/* Remove matching handler. */
			if (gpio_ints[i] == g) {
				/* Disable the interrupt */
				NRF51_GPIOTE_INTENCLR =
					1 << NRF51_GPIOTE_IN_BIT(i);
				/* Zero the handler */
				gpio_ints[i] = NULL;
			}
		}
	} else {
		/* Disable the interrupt */
		NRF51_GPIOTE_INTENCLR = 1 << NRF51_GPIOTE_PORT_BIT;
		/* Zero the shared handler */
		gpio_int_port = NULL;
	}

	return EC_SUCCESS;
}

/*
 * Clear interrupt and run handler.
 */
void gpio_interrupt(void)
{
	const struct gpio_info *g;
	int i;

	for (i = 0; i < NRF51_GPIOTE_IN_COUNT; i++) {
		if (NRF51_GPIOTE_IN(i)) {
			NRF51_GPIOTE_IN(i) = 0;
			g = gpio_ints[i];
			if (g && g->irq_handler)
				g->irq_handler(g - gpio_list);
		}
	}

	if (NRF51_GPIOTE_PORT) {
		NRF51_GPIOTE_PORT = 0;
		g = gpio_int_port;
		if (g && g->irq_handler)
			g->irq_handler(g - gpio_list);
	}
}
DECLARE_IRQ(NRF51_PERID_GPIOTE, gpio_interrupt, 1);
