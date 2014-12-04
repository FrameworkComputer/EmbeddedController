/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"

/* GPIOs are split into 2 words of 16 GPIOs */
#define GPIO_WORD_SIZE_LOG2 4
#define GPIO_WORD_SIZE      (1 << GPIO_WORD_SIZE_LOG2)
#define GPIO_WORD_MASK      (GPIO_WORD_SIZE - 1)

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	uint32_t wi = signal >> GPIO_WORD_SIZE_LOG2;
	uint32_t mask = 1 << (signal & GPIO_WORD_MASK);
	return !!(GR_GPIO_DATAIN(wi) & mask);
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	uint32_t wi = signal >> GPIO_WORD_SIZE_LOG2;
	uint32_t idx = signal & GPIO_WORD_MASK;

	GR_GPIO_MASKBYTE(wi, 1 << idx) = value << idx;
}

static void gpio_set_flags_internal(enum gpio_signal signal, uint32_t port,
				    uint32_t mask, uint32_t flags)
{
	uint32_t wi = signal >> GPIO_WORD_SIZE_LOG2;
	uint32_t idx = signal & GPIO_WORD_MASK;
	uint32_t di = 31 - __builtin_clz(mask);

	/* Set up pullup / pulldown */
	if (flags & GPIO_PULL_UP)
		GR_PINMUX_DIO_CTL(port, di) |= PINMUX_DIO_CTL_PU;
	else
		GR_PINMUX_DIO_CTL(port, di) &= ~PINMUX_DIO_CTL_PU;
	if (flags & GPIO_PULL_DOWN)
		GR_PINMUX_DIO_CTL(port, di) |= PINMUX_DIO_CTL_PD;
	else
		GR_PINMUX_DIO_CTL(port, di) &= ~PINMUX_DIO_CTL_PD;

	if (flags & GPIO_OUTPUT) {
		/* Set pin level first to avoid glitching. */
		GR_GPIO_MASKBYTE(wi, 1 << idx) = !!(flags & GPIO_HIGH) << idx;
		/* Switch Output Enable */
		GR_GPIO_SETDOUTEN(wi) = 1 << idx;
	} else if (flags & GPIO_INPUT) {
		GR_GPIO_CLRDOUTEN(wi) = 1 << idx;
	}

	/* Edge-triggered interrupt */
	if (flags & (GPIO_INT_F_FALLING | GPIO_INT_F_RISING)) {
		GR_GPIO_SETINTTYPE(wi) = 1 << idx;
		if (flags & GPIO_INT_F_RISING)
			GR_GPIO_SETINTPOL(wi) = 1 << idx;
		else
			GR_GPIO_CLRINTPOL(wi) = 1 << idx;
	}
	/* Level-triggered interrupt */
	if (flags & (GPIO_INT_F_LOW | GPIO_INT_F_HIGH)) {
		GR_GPIO_CLRINTTYPE(wi) = 1 << idx;
		if (flags & GPIO_INT_F_HIGH)
			GR_GPIO_SETINTPOL(wi) = 1 << idx;
		else
			GR_GPIO_CLRINTPOL(wi) = 1 << idx;
	}
	/* Interrupt is enabled by gpio_enable_interrupt() */
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	const struct gpio_info *g = gpio_list;
	int i;

	for (i = 0; i < GPIO_COUNT; i++, g++)
		if ((g->port == port) && (g->mask & mask))
			gpio_set_flags_internal(i, port, mask, flags);
}

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	uint32_t di = 31 - __builtin_clz(mask);

	/* Connect the DIO pad to a specific function */
	GR_PINMUX_DIO_SEL(port, di) = PINMUX_FUNC(func);
	/* Connect the specific function to the DIO pad */
	PINMUX_SEL_REG(func) = PINMUX_DIO_SEL(port, di);

	/* Input Enable (if pad used for digital function) */
	GR_PINMUX_DIO_CTL(port, di) |= PINMUX_DIO_CTL_IE;
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	uint32_t wi = signal >> GPIO_WORD_SIZE_LOG2;
	uint32_t idx = signal & GPIO_WORD_MASK;

	GR_GPIO_SETINTEN(wi) = 1 << idx;

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	uint32_t wi = signal >> GPIO_WORD_SIZE_LOG2;
	uint32_t idx = signal & GPIO_WORD_MASK;

	GR_GPIO_CLRINTEN(wi) = 1 << idx;
	GR_GPIO_CLRINTSTAT(wi) = 1 << idx;

	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int i;

	/* Enable clocks */
	REG_WRITE_MLV(GR_PMU_PERICLKSET0,
		      GC_PMU_PERICLKSET0_DGPIO0_MASK,
		      GC_PMU_PERICLKSET0_DGPIO0_LSB, 1);
	REG_WRITE_MLV(GR_PMU_PERICLKSET0,
		      GC_PMU_PERICLKSET0_DGPIO1_MASK,
		      GC_PMU_PERICLKSET0_DGPIO1_LSB, 1);

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		uint32_t di = 31 - __builtin_clz(g->mask);
		uint32_t wi = i >> GPIO_WORD_SIZE_LOG2;
		uint32_t idx = i & GPIO_WORD_MASK;

		if (g->flags & GPIO_DEFAULT)
			continue;

		/* Set up GPIO based on flags */
		gpio_set_flags_internal(i, g->port, g->mask, g->flags);
		/* Connect the GPIO to the DIO pin through the muxing matrix */
		GR_PINMUX_DIO_CTL(g->port, di) |= PINMUX_DIO_CTL_IE;
		GR_PINMUX_DIO_SEL(g->port, di) = PINMUX_GPIO_SEL(wi, idx);
		GR_PINMUX_GPIO_SEL(wi, idx) = PINMUX_DIO_SEL(g->port, di);
	}
}

static void gpio_init(void)
{
	task_enable_irq(GC_IRQNUM_GPIO0_GPIOCOMBINT);
	task_enable_irq(GC_IRQNUM_GPIO1_GPIOCOMBINT);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interrupt handler */

static void gpio_interrupt(int wi)
{
	int idx;
	const struct gpio_info *g = gpio_list;
	uint32_t pending = GR_GPIO_CLRINTSTAT(wi);

	while (pending) {
		idx = get_next_bit(&pending) + wi * 16;
		if (g[idx].irq_handler)
			g[idx].irq_handler(idx);
		/* clear the interrupt */
		GR_GPIO_CLRINTSTAT(wi) = 1 << idx;
	}
}

void _gpio0_interrupt(void)
{
	gpio_interrupt(0);
}
void _gpio1_interrupt(void)
{
	gpio_interrupt(1);
}
DECLARE_IRQ(GC_IRQNUM_GPIO0_GPIOCOMBINT, _gpio0_interrupt, 1);
DECLARE_IRQ(GC_IRQNUM_GPIO1_GPIOCOMBINT, _gpio1_interrupt, 1);
