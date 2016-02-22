/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"

/*
 * The Cr50's ARM core has two GPIO ports of 16 bits each. Each GPIO signal
 * can be routed through a full NxM crossbar to any of a number of external
 * pins. When setting up GPIOs, both the ARM core and the crossbar must be
 * configured correctly. This file is only concerned with the ARM core.
 */

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	return !!(GR_GPIO_DATAIN(g->port) & g->mask);
}

static void set_one_gpio_bit(uint32_t port, uint16_t mask, int value)
{
	if (!mask)
		return;

	/* Assumes mask has one and only one bit set */
	if (mask & 0x00FF)
		GR_GPIO_MASKLOWBYTE(port, mask) = value ? mask : 0;
	else
		GR_GPIO_MASKHIGHBYTE(port, mask >> 8) = value ? mask : 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	const struct gpio_info *g = gpio_list + signal;
	set_one_gpio_bit(g->port, g->mask, value);
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	/* Only matters for outputs */
	if (flags & GPIO_LOW)
		set_one_gpio_bit(port, mask, 0);
	else if (flags & GPIO_HIGH)
		set_one_gpio_bit(port, mask, 1);

	/* Output must be enabled; input is always enabled */
	if (flags & GPIO_OUTPUT)
		GR_GPIO_SETDOUTEN(port) = mask;
	else
		GR_GPIO_CLRDOUTEN(port) = mask;

	/* Interrupt types */
	if (flags & GPIO_INT_F_LOW) {
		GR_GPIO_CLRINTTYPE(port) = mask;
		GR_GPIO_CLRINTPOL(port) = mask;
		GR_GPIO_SETINTEN(port) = mask;
	}
	if (flags & GPIO_INT_F_HIGH) {
		GR_GPIO_CLRINTTYPE(port) = mask;
		GR_GPIO_SETINTPOL(port) = mask;
		GR_GPIO_SETINTEN(port) = mask;
	}
	if (flags & GPIO_INT_F_FALLING) {
		GR_GPIO_SETINTTYPE(port) = mask;
		GR_GPIO_CLRINTPOL(port) = mask;
		GR_GPIO_SETINTEN(port) = mask;
	}
	if (flags & GPIO_INT_F_RISING) {
		GR_GPIO_SETINTTYPE(port) = mask;
		GR_GPIO_SETINTPOL(port) = mask;
		GR_GPIO_SETINTEN(port) = mask;
	}

	/* No way to trigger on both rising and falling edges, darn it. */
}

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	/* This HW feature is not present in the Cr50 ARM core */
}

/*
 * A pinmux_config contains the selector offset and selector value for a
 * particular pinmux entry.
 */
struct pinmux_config {
	uint16_t offset;
	uint16_t value;
};

#define PINMUX_CONFIG(name) {						\
		.offset = CONCAT3(GC_PINMUX_, name, _SEL_OFFSET),	\
		.value  = CONCAT3(GC_PINMUX_, name, _SEL),		\
	}

/*
 * The pinmux struct contains a full description of the connection of a DIO to
 * a GPIO, an internal peripheral, or as a direct input.  The flag
 * DIO_TO_PERIPHERAL is used to select between the two union entries.  There
 * is no union entry for direct input because it requires no parameters.
 */
struct pinmux {
	union {
		enum gpio_signal     signal;
		struct pinmux_config peripheral;
	};
	struct pinmux_config dio;
	uint16_t             flags;
};

/*
 * These macros are used to add flags indicating the type of mapping requested.
 * DIO_TO_PERIPHERAL for FUNC mappings.
 * DIO_ENABLE_DIRECT_INPUT for DIRECT mappings.
 */
#define FLAGS_FUNC(name) DIO_TO_PERIPHERAL
#define FLAGS_GPIO(name) 0
#define FLAGS_DIRECT     DIO_ENABLE_DIRECT_INPUT

/*
 * These macros are used to selectively initialize the anonymous union based
 * on the type of pinmux mapping requested (FUNC, GPIO, or DIRECT).
 */
#define PINMUX_FUNC(name) .peripheral = PINMUX_CONFIG(name),
#define PINMUX_GPIO(name) .signal     = CONCAT2(GPIO_, name),
#define PINMUX_DIRECT

/*
 * Initialize an entry for the pinmux list.  The first parameter can be either
 * FUNC(name) or GPIO(name) depending on the type of mapping required.  The
 * second argument is the DIO name to map to.  And the final argument is the
 * flags set for this mapping, this macro adds the DIO_TO_PERIPHERAL flag for
 * a FUNC mapping.
 */
#define PINMUX(name, dio_name, dio_flags) {		\
		PINMUX_##name				\
		.dio   = PINMUX_CONFIG(DIO##dio_name),	\
		.flags = dio_flags | FLAGS_##name	\
	},

static const struct pinmux pinmux_list[] = {
	#include "gpio.wrap"
};

static int connect_dio_to_peripheral(struct pinmux const *p)
{
	if (p->flags & DIO_OUTPUT)
		DIO_SEL_REG(p->dio.offset) = p->peripheral.value;

	if (p->flags & DIO_INPUT)
		DIO_SEL_REG(p->peripheral.offset) = p->dio.value;

	return p->flags & DIO_INPUT;
}

static int connect_dio_to_gpio(struct pinmux const *p)
{
	const struct gpio_info *g = gpio_list + p->signal;
	int bitnum = GPIO_MASK_TO_NUM(g->mask);

	if ((g->flags & GPIO_OUTPUT) || (p->flags & DIO_OUTPUT))
		DIO_SEL_REG(p->dio.offset) = GET_GPIO_FUNC(g->port, bitnum);

	if ((g->flags & GPIO_INPUT) || (p->flags & DIO_INPUT))
		GET_GPIO_SEL_REG(g->port, bitnum) = p->dio.value;

	return (g->flags & GPIO_INPUT) || (p->flags & DIO_INPUT);
}

static void connect_pinmux(struct pinmux const *p)
{
	if ((p->flags & DIO_ENABLE_DIRECT_INPUT) ||
	    ((p->flags & DIO_TO_PERIPHERAL) ?
	     connect_dio_to_peripheral(p) :
	     connect_dio_to_gpio(p)))
		REG_WRITE_MLV(DIO_CTL_REG(p->dio.offset),
			      DIO_CTL_IE_MASK,
			      DIO_CTL_IE_LSB, 1);
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	GR_GPIO_SETINTEN(g->port) = g->mask;
	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	GR_GPIO_CLRINTEN(g->port) = g->mask;
	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;

	int i;

	/* Enable clocks */
	REG_WRITE_MLV(GR_PMU_PERICLKSET0,
		      GC_PMU_PERICLKSET0_DGPIO0_CLK_MASK,
		      GC_PMU_PERICLKSET0_DGPIO0_CLK_LSB, 1);
	REG_WRITE_MLV(GR_PMU_PERICLKSET0,
		      GC_PMU_PERICLKSET0_DGPIO1_CLK_MASK,
		      GC_PMU_PERICLKSET0_DGPIO1_CLK_LSB, 1);

	/* Set up the pinmux */
	for (i = 0; i < ARRAY_SIZE(pinmux_list); i++)
		connect_pinmux(pinmux_list + i);

	/* Set up ARM core GPIOs */
	for (i = 0; i < GPIO_COUNT; i++, g++)
		if (g->mask && !(g->flags & GPIO_DEFAULT))
			gpio_set_flags_by_mask(g->port, g->mask, g->flags);
}

static void gpio_init(void)
{
	task_enable_irq(GC_IRQNUM_GPIO0_GPIOCOMBINT);
	task_enable_irq(GC_IRQNUM_GPIO1_GPIOCOMBINT);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interrupt handler stuff */

static void gpio_invoke_handler(uint32_t port, uint32_t mask)
{
	const struct gpio_info *g = gpio_list;
	int i;
	for (i = 0; i < GPIO_IH_COUNT; i++, g++)
		if (port == g->port && (mask & g->mask))
			gpio_irq_handlers[i](i);
}

static void gpio_interrupt(int port)
{
	int bitnum;
	uint32_t mask;
	uint32_t pending = GR_GPIO_CLRINTSTAT(port);

	while (pending) {
		bitnum = GPIO_MASK_TO_NUM(pending);
		mask = 1 << bitnum;
		pending &= ~mask;
		gpio_invoke_handler(port, mask);
		GR_GPIO_CLRINTSTAT(port) = mask;
	}
}

void _gpio0_interrupt(void)
{
	gpio_interrupt(GPIO_0);
}
void _gpio1_interrupt(void)
{
	gpio_interrupt(GPIO_1);
}
DECLARE_IRQ(GC_IRQNUM_GPIO0_GPIOCOMBINT, _gpio0_interrupt, 1);
DECLARE_IRQ(GC_IRQNUM_GPIO1_GPIOCOMBINT, _gpio1_interrupt, 1);
