/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "board.h"
#include "gpio.h"
#include "hooks.h"
#include "power_button.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"


/* 0-terminated list of GPIO bases */
static const uint32_t gpio_bases[] = {
	LM4_GPIO_A, LM4_GPIO_B, LM4_GPIO_C, LM4_GPIO_D,
	LM4_GPIO_E, LM4_GPIO_F, LM4_GPIO_G, LM4_GPIO_H,
	LM4_GPIO_J, LM4_GPIO_K, LM4_GPIO_L, LM4_GPIO_M,
	LM4_GPIO_N, LM4_GPIO_P, LM4_GPIO_Q, 0
};


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
	int is_warm = 0;
	int i;

	if (LM4_SYSTEM_RCGCGPIO == 0x7fff) {
		/* This is a warm reboot */
		is_warm = 1;
	} else {
		/* Enable clocks to all the GPIO blocks (since we use all of
		 * them as GPIOs) */
		LM4_SYSTEM_RCGCGPIO |= 0x7fff;
		scratch = LM4_SYSTEM_RCGCGPIO;  /* Delay a few clocks */
	}

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

	/* Mask all GPIO interrupts */
	for (i = 0; gpio_bases[i]; i++)
		LM4_GPIO_IM(gpio_bases[i]) = 0;

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {

		/* Use as GPIO, not alternate function */
		gpio_set_alternate_function(g->port, g->mask, 0);

		/* Set up GPIO based on flags */
		gpio_set_flags(i, g->flags);

		/* If this is a cold boot, set the level.  On a warm reboot,
		 * leave things where they were or we'll shut off the x86. */
		if ((g->flags & GPIO_OUTPUT) && !is_warm)
			gpio_set_level(i, g->flags & GPIO_HIGH);
	}

	return EC_SUCCESS;
}


static int gpio_init(void)
{
	/* Enable IRQs now that pins are set up */
	task_enable_irq(LM4_IRQ_GPIOA);
	task_enable_irq(LM4_IRQ_GPIOB);
	task_enable_irq(LM4_IRQ_GPIOC);
	task_enable_irq(LM4_IRQ_GPIOD);
	task_enable_irq(LM4_IRQ_GPIOE);
	task_enable_irq(LM4_IRQ_GPIOF);
	task_enable_irq(LM4_IRQ_GPIOG);
	task_enable_irq(LM4_IRQ_GPIOH);
	task_enable_irq(LM4_IRQ_GPIOJ);
	task_enable_irq(LM4_IRQ_GPIOK);
	task_enable_irq(LM4_IRQ_GPIOL);
	task_enable_irq(LM4_IRQ_GPIOM);
#if defined(KB_SCAN_ROW_IRQ) && (KB_SCAN_ROW_IRQ != LM4_IRQ_GPION)
	task_enable_irq(LM4_IRQ_GPION);
#endif
	task_enable_irq(LM4_IRQ_GPIOP);
	task_enable_irq(LM4_IRQ_GPIOQ);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);


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


const char *gpio_get_name(enum gpio_signal signal)
{
	return gpio_list[signal].name;
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


int gpio_set_flags(enum gpio_signal signal, int flags)
{
	const struct gpio_info *g = gpio_list + signal;

	if (flags & GPIO_DEFAULT)
		return EC_SUCCESS;
	if (flags & GPIO_OUTPUT) {
		/* Output */
		/* Select open drain first, so that we don't glitch the signal
		 * when changing the line to an output. */
		if (g->flags & GPIO_OPEN_DRAIN)
			LM4_GPIO_ODR(g->port) |= g->mask;
		else
			LM4_GPIO_ODR(g->port) &= ~g->mask;

		LM4_GPIO_DIR(g->port) |= g->mask;
	} else {
		/* Input */
		LM4_GPIO_DIR(g->port) &= ~g->mask;

		if (g->flags & GPIO_PULL) {
			/* With pull up/down */
			if (g->flags & GPIO_HIGH)
				LM4_GPIO_PUR(g->port) |= g->mask;
			else
				LM4_GPIO_PDR(g->port) |= g->mask;
		} else {
			/* No pull up/down */
			LM4_GPIO_PUR(g->port) &= ~g->mask;
			LM4_GPIO_PDR(g->port) &= ~g->mask;
		}
	}

	/* Set up interrupt type */
	if (g->flags & GPIO_INT_LEVEL)
		LM4_GPIO_IS(g->port) |= g->mask;
	else
		LM4_GPIO_IS(g->port) &= ~g->mask;

	if (g->flags & (GPIO_INT_RISING | GPIO_INT_HIGH))
		LM4_GPIO_IEV(g->port) |= g->mask;
	else
		LM4_GPIO_IEV(g->port) &= ~g->mask;

	if (g->flags & GPIO_INT_BOTH)
		LM4_GPIO_IBE(g->port) |= g->mask;
	else
		LM4_GPIO_IBE(g->port) &= ~g->mask;

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
#define GPIO_IRQ_FUNC(irqfunc, gpiobase)		\
	static void irqfunc(void)			\
	{						\
		uint32_t mis = LM4_GPIO_MIS(gpiobase);	\
		LM4_GPIO_ICR(gpiobase) = mis;		\
		gpio_interrupt(gpiobase, mis);		\
	}

GPIO_IRQ_FUNC(__gpio_a_interrupt, LM4_GPIO_A);
GPIO_IRQ_FUNC(__gpio_b_interrupt, LM4_GPIO_B);
GPIO_IRQ_FUNC(__gpio_c_interrupt, LM4_GPIO_C);
GPIO_IRQ_FUNC(__gpio_d_interrupt, LM4_GPIO_D);
GPIO_IRQ_FUNC(__gpio_e_interrupt, LM4_GPIO_E);
GPIO_IRQ_FUNC(__gpio_f_interrupt, LM4_GPIO_F);
GPIO_IRQ_FUNC(__gpio_g_interrupt, LM4_GPIO_G);
GPIO_IRQ_FUNC(__gpio_h_interrupt, LM4_GPIO_H);
GPIO_IRQ_FUNC(__gpio_j_interrupt, LM4_GPIO_J);
GPIO_IRQ_FUNC(__gpio_k_interrupt, LM4_GPIO_K);
GPIO_IRQ_FUNC(__gpio_l_interrupt, LM4_GPIO_L);
GPIO_IRQ_FUNC(__gpio_m_interrupt, LM4_GPIO_M);
#if defined(KB_SCAN_ROW_GPIO) && (KB_SCAN_ROW_GPIO != LM4_GPIO_N)
GPIO_IRQ_FUNC(__gpio_n_interrupt, LM4_GPIO_N);
#endif
GPIO_IRQ_FUNC(__gpio_p_interrupt, LM4_GPIO_P);
GPIO_IRQ_FUNC(__gpio_q_interrupt, LM4_GPIO_Q);

#undef GPIO_IRQ_FUNC

/* Declare IRQs */
/* TODO: nesting this macro inside the GPIO_IRQ_FUNC macro works poorly because
 * DECLARE_IRQ() stringizes its inputs. */
DECLARE_IRQ(LM4_IRQ_GPIOA, __gpio_a_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOB, __gpio_b_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOC, __gpio_c_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOD, __gpio_d_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOE, __gpio_e_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOF, __gpio_f_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOG, __gpio_g_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOH, __gpio_h_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOJ, __gpio_j_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOK, __gpio_k_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOL, __gpio_l_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOM, __gpio_m_interrupt, 1);
#if defined(KB_SCAN_ROW_GPIO) && (KB_SCAN_ROW_GPIO != LM4_GPIO_N)
DECLARE_IRQ(LM4_IRQ_GPION, __gpio_n_interrupt, 1);
#endif
DECLARE_IRQ(LM4_IRQ_GPIOP, __gpio_p_interrupt, 1);
DECLARE_IRQ(LM4_IRQ_GPIOQ, __gpio_q_interrupt, 1);
