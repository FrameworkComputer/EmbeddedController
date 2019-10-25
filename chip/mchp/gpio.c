/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for MCHP MEC */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "lpc_chip.h"
#include "tfdp_chip.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)


struct gpio_int_mapping {
	int8_t girq_id;
	int8_t port_offset;
};

/*
 * Mapping from GPIO port to GIRQ info
 * MEC17xx each bank contains 32 GPIO's.
 * Pin Id is the bit position [0:31]
 * Bank		GPIO's		GIRQ
 * 0		0000 - 0036	11
 * 1		0040 - 0076	10
 * 2		0100 - 0135	9
 * 3		0140 - 0175	8
 * 4		0200 - 0235	12
 * 5		0240 - 0276	26
 */
static const struct gpio_int_mapping int_map[6] = {
	{ 11, 0 }, { 10, 1 }, { 9, 2 },
	{ 8, 3 }, { 12, 4 }, { 26, 5 }
};



/*
 * NOTE: GCC __builtin_ffs(val) returns (index + 1) of least significant
 * 1-bit of val or if val == 0 returns 0
 */
void gpio_set_alternate_function(uint32_t port, uint32_t mask,
				enum gpio_alternate_func func)
{
	int i;
	uint32_t val;

	while (mask) {
		i = __builtin_ffs(mask) - 1;
		val = MCHP_GPIO_CTL(port, i);
		val &= ~(BIT(12) | BIT(13));
		/* mux_control = DEFAULT, indicates GPIO */
		if (func > GPIO_ALT_FUNC_DEFAULT)
			val |= (func & 0x3) << 12;
		MCHP_GPIO_CTL(port, i) = val;
		mask &= ~BIT(i);
	}
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	uint32_t mask = gpio_list[signal].mask;
	int i;
	uint32_t val;

	if (mask == 0)
		return 0;
	i = GPIO_MASK_TO_NUM(mask);
	val = MCHP_GPIO_CTL(gpio_list[signal].port, i);

	return (val & BIT(24)) ? 1 : 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	uint32_t mask = gpio_list[signal].mask;
	int i;

	if (mask == 0)
		return;
	i = GPIO_MASK_TO_NUM(mask);

	if (value)
		MCHP_GPIO_CTL(gpio_list[signal].port, i) |= BIT(16);
	else
		MCHP_GPIO_CTL(gpio_list[signal].port, i) &= ~BIT(16);
}

/*
 * Add support for new #ifdef CONFIG_CMD_GPIO_POWER_DOWN.
 * If GPIO_POWER_DONW flag is set force GPIO Control to
 * GPIO input, interrupt detect disabled, power control field
 * in bits[3:2]=10b.
 * NOTE: if interrupt detect is enabled when pin is powered down
 * then a false edge may be detected.
 *
 */
void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	int i;
	uint32_t val;

	while (mask) {
		i = GPIO_MASK_TO_NUM(mask);
		mask &= ~BIT(i);
		val = MCHP_GPIO_CTL(port, i);

#ifdef CONFIG_GPIO_POWER_DOWN
		if (flags & GPIO_POWER_DOWN) {
			val = (MCHP_GPIO_CTRL_PWR_OFF +
					MCHP_GPIO_INTDET_DISABLED);
			MCHP_GPIO_CTL(port, i) = val;
			continue;
		}
#endif
		val &= ~(MCHP_GPIO_CTRL_PWR_MASK);
		val |= MCHP_GPIO_CTRL_PWR_VTR;

		/*
		 * Select open drain first, so that we don't
		 * glitch the signal when changing the line to
		 * an output.
		 */
		if (flags & GPIO_OPEN_DRAIN)
			val |= (MCHP_GPIO_OPEN_DRAIN);
		else
			val &= ~(MCHP_GPIO_OPEN_DRAIN);

		if (flags & GPIO_OUTPUT) {
			val |= (MCHP_GPIO_OUTPUT);
			val &= ~(MCHP_GPIO_OUTSEL_PAR);
		} else {
			val &= ~(MCHP_GPIO_OUTPUT);
			val |= (MCHP_GPIO_OUTSEL_PAR);
		}

		/* Handle pull-up / pull-down */
		val &= ~(MCHP_GPIO_CTRL_PUD_MASK);
		if (flags & GPIO_PULL_UP)
			val |= MCHP_GPIO_CTRL_PUD_PU;
		else if (flags & GPIO_PULL_DOWN)
			val |= MCHP_GPIO_CTRL_PUD_PD;
		else
			val |= MCHP_GPIO_CTRL_PUD_NONE;

		/* Set up interrupt */
		val &= ~(MCHP_GPIO_INTDET_MASK);
		switch (flags & GPIO_INT_ANY) {
		case GPIO_INT_F_RISING:
			val |= MCHP_GPIO_INTDET_EDGE_RIS;
			break;
		case GPIO_INT_F_FALLING:
			val |= MCHP_GPIO_INTDET_EDGE_FALL;
			break;
		case GPIO_INT_BOTH: /* both edges */
			val |= MCHP_GPIO_INTDET_EDGE_BOTH;
			break;
		case GPIO_INT_F_LOW:
			val |= MCHP_GPIO_INTDET_LVL_LO;
			break;
		case GPIO_INT_F_HIGH:
			val |= MCHP_GPIO_INTDET_LVL_HI;
			break;
		default:
			val |= MCHP_GPIO_INTDET_DISABLED;
			break;
		}

		/* Set up level */
		if (flags & GPIO_HIGH)
			val |= (MCHP_GPIO_CTRL_OUT_LVL);
		else if (flags & GPIO_LOW)
			val &= ~(MCHP_GPIO_CTRL_OUT_LVL);

		MCHP_GPIO_CTL(port, i) = val;
	}
}

void gpio_power_off_by_mask(uint32_t port, uint32_t mask)
{
	int i;

	while (mask) {
		i = GPIO_MASK_TO_NUM(mask);
		mask &= ~BIT(i);

		MCHP_GPIO_CTL(port, i) = (MCHP_GPIO_CTRL_PWR_OFF +
					MCHP_GPIO_INTDET_DISABLED);
	}
}

int gpio_power_off(enum gpio_signal signal)
{
	int i, port;

	if (gpio_list[signal].mask == 0)
		return EC_ERROR_INVAL;

	i = GPIO_MASK_TO_NUM(gpio_list[signal].mask);
	port = gpio_list[signal].port;

	MCHP_GPIO_CTL(port, i) = (MCHP_GPIO_CTRL_PWR_OFF +
			MCHP_GPIO_INTDET_DISABLED);

	return EC_SUCCESS;
}

/*
 * gpio_list[signal].port = [0, 6] each port contains up to 32 pins
 * gpio_list[signal].mask = bit mask in 32-bit port
 * NOTE: MCHP GPIO are always aggregated not direct connected to NVIC.
 * GPIO's are aggregated into banks of 32 pins.
 * Each bank/port are connected to a GIRQ.
 * int_map[port].girq_id is the GIRQ ID
 * The bit number in the GIRQ registers is the same as the bit number
 * in the GPIO bank.
 */
int gpio_enable_interrupt(enum gpio_signal signal)
{
	int i, port, girq_id;

	if (gpio_list[signal].mask == 0)
		return EC_SUCCESS;

	i = GPIO_MASK_TO_NUM(gpio_list[signal].mask);
	port = gpio_list[signal].port;
	girq_id = int_map[port].girq_id;

	MCHP_INT_ENABLE(girq_id) = BIT(i);
	MCHP_INT_BLK_EN |= BIT(girq_id);

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	int i, port, girq_id;

	if (gpio_list[signal].mask == 0)
		return EC_SUCCESS;

	i = GPIO_MASK_TO_NUM(gpio_list[signal].mask);
	port = gpio_list[signal].port;
	girq_id = int_map[port].girq_id;


	MCHP_INT_DISABLE(girq_id) = BIT(i);

	return EC_SUCCESS;
}

/*
 * MCHP Interrupt Source is R/W1C no need for read-modify-write.
 * GPIO's are aggregated meaning the NVIC Pending bit may be
 * set for another GPIO in the GIRQ. You can clear NVIC pending
 * and the hardware should re-assert it within one Cortex-M4 clock.
 * If the Cortex-M4 is clocked slower than AHB then the Cortex-M4
 * will take longer to register the interrupt. Not clearing NVIC
 * pending leave a pending status if only the GPIO this routine
 * clears is pending.
 * NVIC (system control) register space is strongly-ordered
 * Interrupt Aggregator is in Device space (system bus connected
 * to AHB) with the Cortex-M4 write buffer.
 * We need to insure the write to aggregator register in device
 * AHB space completes before NVIC pending is cleared.
 * The Cortex-M4 memory ordering rules imply Device access
 * comes before strongly ordered access. Cortex-M4 will not re-order
 * the writes. Due to the presence of the write buffer a DSB will
 * not guarantee the clearing of the device status completes. Add
 * a read back before clearing NVIC pending.
 * GIRQ 8, 9, 10, 11, 12, 26 map to NVIC inputs 0, 1, 2, 3, 4, and 18.
 */
int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	int i, port, girq_id;

	if (gpio_list[signal].mask == 0)
		return EC_SUCCESS;

	i = GPIO_MASK_TO_NUM(gpio_list[signal].mask);
	port = gpio_list[signal].port;
	girq_id = int_map[port].girq_id;

	/* Clear interrupt source sticky status bit even if not enabled */
	MCHP_INT_SOURCE(girq_id) = BIT(i);
	i = MCHP_INT_SOURCE(girq_id);
	task_clear_pending_irq(girq_id - 8);

	return EC_SUCCESS;
}

/*
 * MCHP NOTE - called from main before scheduler started
 */
void gpio_pre_init(void)
{
	int i;
	int flags;
	int is_warm = system_is_reboot_warm();
	const struct gpio_info *g = gpio_list;


	for (i = 0; i < GPIO_COUNT; i++, g++) {
		flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		/*
		 * If this is a warm reboot, don't set the output levels or
		 * we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		gpio_set_flags_by_mask(g->port, g->mask, flags);

		/* Use as GPIO, not alternate function */
		gpio_set_alternate_function(g->port, g->mask,
					GPIO_ALT_FUNC_NONE);
	}
}

/* Clear any interrupt flags before enabling GPIO interrupt
 * Original code has flaws.
 * Writing result register to source only clears bits that have their
 * enable and sources bits set.
 * We must clear the NVIC pending R/W bit before setting NVIC enable.
 * NVIC Pending is only cleared by the NVIC HW on ISR entry.
 * Modifications are:
 * 1. Clear all status bits in each GPIO GIRQ. This assumes any edges
 *    will occur after gpio_init. The old code is also making this
 *    assumption for the GPIO's that have been enabled.
 * 2. Clear NVIC pending to prevent ISR firing on false edge.
 */
#define ENABLE_GPIO_GIRQ(x) \
	do { \
		MCHP_INT_SOURCE(x) = 0xfffffffful; \
		task_clear_pending_irq(MCHP_IRQ_GIRQ ## x); \
		task_enable_irq(MCHP_IRQ_GIRQ ## x); \
	} while (0)


static void gpio_init(void)
{
	ENABLE_GPIO_GIRQ(8);
	ENABLE_GPIO_GIRQ(9);
	ENABLE_GPIO_GIRQ(10);
	ENABLE_GPIO_GIRQ(11);
	ENABLE_GPIO_GIRQ(12);
	ENABLE_GPIO_GIRQ(26);
}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/************************************************************************/
/* Interrupt handlers */


/**
 * Handler for each GIRQ interrupt. This reads and clears the interrupt
 * bits for the GIRQ interrupt, then finds and calls the corresponding
 * GPIO interrupt handlers.
 *
 * @param girq		GIRQ index
 * @param port	zero based GPIO port number [0, 5]
 * @note __builtin_ffs(x) returns bitpos+1 of least significant 1-bit
 * in x or 0 if no bits are set.
 */
static void gpio_interrupt(int girq, int port)
{
	int i, bit;
	const struct gpio_info *g = gpio_list;
	uint32_t sts = MCHP_INT_RESULT(girq);

	/* RW1C, no need for read-modify-write */
	MCHP_INT_SOURCE(girq) = sts;

	trace12(0, GPIO, 0, "GPIO GIRQ %d result = 0x%08x", girq, sts);
	trace12(0, GPIO, 0, "GPIO ParIn[%d]      = 0x%08x",
		port, MCHP_GPIO_PARIN(port));

	for (i = 0; (i < GPIO_IH_COUNT) && sts; ++i, ++g) {
		if (g->port != port)
			continue;

		bit = __builtin_ffs(g->mask);
		if (bit) {
			bit--;
			if (sts & BIT(bit)) {
				trace12(0, GPIO, 0,
					"Bit[%d]: handler @ 0x%08x", bit,
					(uint32_t)gpio_irq_handlers[i]);
				gpio_irq_handlers[i](i);
			}
			sts &= ~BIT(bit);
		}
	}
}

#define GPIO_IRQ_FUNC(irqfunc, girq, port)\
	void irqfunc(void) \
	{ \
		gpio_interrupt(girq, port);\
	}

GPIO_IRQ_FUNC(__girq_8_interrupt, 8, 3);
GPIO_IRQ_FUNC(__girq_9_interrupt, 9, 2);
GPIO_IRQ_FUNC(__girq_10_interrupt, 10, 1);
GPIO_IRQ_FUNC(__girq_11_interrupt, 11, 0);
GPIO_IRQ_FUNC(__girq_12_interrupt, 12, 4);
GPIO_IRQ_FUNC(__girq_26_interrupt, 26, 5);

#undef GPIO_IRQ_FUNC

/*
 * Declare IRQs.  Nesting this macro inside the GPIO_IRQ_FUNC macro works
 * poorly because DECLARE_IRQ() stringizes its inputs.
 */
DECLARE_IRQ(MCHP_IRQ_GIRQ8, __girq_8_interrupt, 1);
DECLARE_IRQ(MCHP_IRQ_GIRQ9, __girq_9_interrupt, 1);
DECLARE_IRQ(MCHP_IRQ_GIRQ10, __girq_10_interrupt, 1);
DECLARE_IRQ(MCHP_IRQ_GIRQ11, __girq_11_interrupt, 1);
DECLARE_IRQ(MCHP_IRQ_GIRQ12, __girq_12_interrupt, 1);
DECLARE_IRQ(MCHP_IRQ_GIRQ26, __girq_26_interrupt, 1);

