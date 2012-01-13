/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "power_button.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"


/* 0-terminated list of GPIO bases */
const uint32_t gpio_bases[] = {
	LM4_GPIO_A, LM4_GPIO_B, LM4_GPIO_C, LM4_GPIO_D,
	LM4_GPIO_E, LM4_GPIO_F, LM4_GPIO_G, LM4_GPIO_H,
	LM4_GPIO_J, LM4_GPIO_K, LM4_GPIO_L, LM4_GPIO_M,
	LM4_GPIO_N, LM4_GPIO_P, LM4_GPIO_Q, 0
};


/* Signal information from board.c.  Must match order from enum gpio_signal. */
extern const struct gpio_info gpio_list[GPIO_COUNT];


/* Find a GPIO signal by name.  Returns the signal index, or GPIO_COUNT if
 * no match. */
static enum gpio_signal find_signal_by_name(const char *name)
{
	const struct gpio_info *g = gpio_list;
	int i;

	if (!name || !*name)
		return GPIO_COUNT;

	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (!strcasecmp(name, g->name))
			return i;
	}

	return GPIO_COUNT;
}


/* Find the index of a GPIO port base address (LM4_GPIO_[A-Q]); this is used by
 * the clock gating registers.  Returns the index, or -1 if no match. */
static int find_gpio_port_index(uint32_t port_base)
{
	int i;
	for (i = 0; gpio_bases[i]; i++) {
		if (gpio_bases[i] == port_base)
			return i;
	}
	return -1;
}


int gpio_pre_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));
	const struct gpio_info *g = gpio_list;
	int i;

	/* Enable clocks to all the GPIO blocks (since we use all of them as
	 * GPIOs) */
	LM4_SYSTEM_RCGCGPIO |= 0x7fff;
	scratch = LM4_SYSTEM_RCGCGPIO;  /* Delay a few clocks */

	/* Disable GPIO commit control for PD7 and PF0, since we don't use the
	 * NMI pin function. */
	LM4_GPIO_LOCK(LM4_GPIO_D) = LM4_GPIO_LOCK_UNLOCK;
	LM4_GPIO_CR(LM4_GPIO_D) |= 0x80;
	LM4_GPIO_LOCK(LM4_GPIO_D) = 0;
	LM4_GPIO_LOCK(LM4_GPIO_F) = LM4_GPIO_LOCK_UNLOCK;
	LM4_GPIO_CR(LM4_GPIO_F) |= 0x01;
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0;

	/* Clear SSI0 alternate function on PA2:5 */
	LM4_GPIO_AFSEL(LM4_GPIO_A) &= ~0x3c;

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {

		/* Handle GPIO direction */
		if (g->flags & GPIO_OUTPUT) {
			/* Output with default level */
			gpio_set_level(i, g->flags & GPIO_HIGH);
			LM4_GPIO_DIR(g->port) |= g->mask;
		} else {
			/* Input */
			if (g->flags & GPIO_PULL) {
				/* With pull up/down */
				if (g->flags & GPIO_HIGH)
					LM4_GPIO_PUR(g->port) |= g->mask;
				else
					LM4_GPIO_PDR(g->port) |= g->mask;
			}
		}

		/* Use as GPIO, not alternate function */
		gpio_set_alternate_function(g->port, g->mask, 0);

		/* Set up interrupts if necessary */
		if (g->flags & GPIO_INT_LEVEL)
			LM4_GPIO_IS(g->port) |= g->mask;
		if (g->flags & (GPIO_INT_RISING | GPIO_INT_HIGH))
			LM4_GPIO_IEV(g->port) |= g->mask;
		if (g->flags & GPIO_INT_BOTH)
			LM4_GPIO_IBE(g->port) |= g->mask;
		/* Interrupt is enabled by gpio_enable_interrupt() */
	}

	return EC_SUCCESS;
}


void gpio_set_alternate_function(int port, int mask, int func)
{
	int port_index = find_gpio_port_index(port);
	int cgmask;

	if (port_index < 0)
		return;  /* TODO: assert */

	/* Enable the GPIO port if necessary */
	cgmask = 1 << port_index;
	if ((LM4_SYSTEM_RCGCGPIO & cgmask) != cgmask) {
		volatile uint32_t scratch  __attribute__((unused));
		LM4_SYSTEM_RCGCGPIO |= cgmask;
		/* Delay a few clocks before accessing GPIO registers on that
		 * port. */
		scratch = LM4_SYSTEM_RCGCGPIO;
	}

	if (func) {
		int pctlmask = 0;
		int i;
		/* Expand mask from bits to nibbles */
		for (i = 0; i < 8; i++) {
			if (mask & (1 << i))
				pctlmask |= 1 << (4 * i);
		}

		LM4_GPIO_PCTL(port) =
			(LM4_GPIO_PCTL(port) & ~(pctlmask * 0xf)) |
			(pctlmask * func);
		LM4_GPIO_AFSEL(port) |= mask;
	} else {
		LM4_GPIO_AFSEL(port) &= ~mask;
	}
	LM4_GPIO_DEN(port) |= mask;
}


int gpio_get_level(enum gpio_signal signal)
{
	return LM4_GPIO_DATA(gpio_list[signal].port,
			     gpio_list[signal].mask) ? 1 : 0;
}


int gpio_set_level(enum gpio_signal signal, int value)
{
	/* Ok to write 0xff becuase LM4_GPIO_DATA bit-masks only the bit
	 * we care about. */
	LM4_GPIO_DATA(gpio_list[signal].port,
		      gpio_list[signal].mask) = (value ? 0xff : 0);
	return EC_SUCCESS;
}


int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	/* Fail if no interrupt handler */
	if (!g->irq_handler)
		return EC_ERROR_UNKNOWN;

	LM4_GPIO_IM(g->port) |= g->mask;
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interrupt handlers */

static void gpio_interrupt(int port, uint32_t mis)
{
	int i = 0;
	const struct gpio_info *g = gpio_list;

	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (port == g->port && (mis & g->mask) && g->irq_handler)
			g->irq_handler(i);
	}
}

/* Handlers for each GPIO port.  These read and clear the interrupt bits for
 * the port, then call the master handler above. */

static void __gpio_c_interrupt(void)
{
	/* Read and clear the interrupt status */
	uint32_t mis = LM4_GPIO_MIS(LM4_GPIO_C);
	LM4_GPIO_ICR(LM4_GPIO_C) = mis;
	gpio_interrupt(LM4_GPIO_C, mis);
}
DECLARE_IRQ(LM4_IRQ_GPIOC, __gpio_c_interrupt, 1);

static void __gpio_k_interrupt(void)
{
	/* Read and clear the interrupt status */
	uint32_t mis = LM4_GPIO_MIS(LM4_GPIO_K);
	LM4_GPIO_ICR(LM4_GPIO_K) = mis;
	gpio_interrupt(LM4_GPIO_K, mis);
}
DECLARE_IRQ(LM4_IRQ_GPIOK, __gpio_k_interrupt, 1);

/*****************************************************************************/
/* Console commands */

static int command_gpio_get(int argc, char **argv)
{
	const struct gpio_info *g = gpio_list;
	int i;

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = find_signal_by_name(argv[1]);
		if (i == GPIO_COUNT) {
			uart_puts("Unknown signal name.\n");
			return EC_ERROR_UNKNOWN;
		}
		g = gpio_list + i;
		uart_printf("  %d %s\n", gpio_get_level(i), g->name);
		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	uart_puts("Current GPIO levels:\n");
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (g->mask)
			uart_printf("  %d %s\n", gpio_get_level(i), g->name);
		/* We have enough GPIOs that we'll overflow the output buffer
		 * without flushing */
		uart_flush_output();
	}
	return EC_SUCCESS;
}


static int command_gpio_set(int argc, char **argv)
{
	const struct gpio_info *g;
	char *e;
	int v, i;

	if (argc < 3) {
		uart_puts("Usage: gpioset <signal_name> <0|1>\n");
		return EC_ERROR_UNKNOWN;
	}

	i = find_signal_by_name(argv[1]);
	if (i == GPIO_COUNT) {
		uart_puts("Unknown signal name.\n");
		return EC_ERROR_UNKNOWN;
	}
	g = gpio_list + i;

	if (!g->mask) {
		uart_puts("Signal is not implemented.\n");
		return EC_ERROR_UNKNOWN;
	}
	if (!(g->flags & GPIO_OUTPUT)) {
		uart_puts("Signal is not an output.\n");
		return EC_ERROR_UNKNOWN;
	}

	v = strtoi(argv[2], &e, 0);
	if (*e) {
		uart_puts("Invalid signal value.\n");
		return EC_ERROR_UNKNOWN;
	}

	return gpio_set_level(i, v);
}


static const struct console_command console_commands[] = {
	{"gpioget", command_gpio_get},
	{"gpioset", command_gpio_set},
};
static const struct console_group command_group = {
	"GPIO", console_commands, ARRAY_SIZE(console_commands)
};

/*****************************************************************************/
/* Initialization */

int gpio_init(void)
{
	console_register_commands(&command_group);
	return EC_SUCCESS;
}
