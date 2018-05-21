/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "kmsc_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*
 * Converts port (ie GPIO A) to base address offset of the control register
 * (GPCRx0) for that port.
 */
#define CTRL_BASE(port) ((port)*8 + ((port) < GPIO_K ? 8 : 56))

/**
 * Convert wake-up controller (WUC) group to the corresponding wake-up edge
 * sense register (WUESR). Return pointer to the register.
 *
 * @param grp  WUC group.
 *
 * @return Pointer to corresponding WUESR register.
 */
static volatile uint8_t *wuesr(uint8_t grp)
{
	/*
	 * From WUESR1-WUESR4, the address increases by ones. From WUESR5 on
	 * the address increases by fours.
	 */
	return (grp <= 4) ?
			(volatile uint8_t *)(IT83XX_WUC_WUESR1 + grp-1) :
			(volatile uint8_t *)(IT83XX_WUC_WUESR5 + 4*(grp-5));
}

/**
 * Convert wake-up controller (WUC) group to the corresponding wake-up edge
 * mode register (WUEMR). Return pointer to the register.
 *
 * @param grp  WUC group.
 *
 * @return Pointer to corresponding WUEMR register.
 */
static volatile uint8_t *wuemr(uint8_t grp)
{
	/*
	 * From WUEMR1-WUEMR4, the address increases by ones. From WUEMR5 on
	 * the address increases by fours.
	 */
	return (grp <= 4) ?
			(volatile uint8_t *)(IT83XX_WUC_WUEMR1 + grp-1) :
			(volatile uint8_t *)(IT83XX_WUC_WUEMR5 + 4*(grp-5));
}

/*
 * Array to store the corresponding GPIO port and mask, and WUC group and mask
 * for each WKO interrupt. This allows GPIO interrupts coming in through WKO
 * to easily identify which pin caused the interrupt.
 * Note: Using designated initializers here in addition to using the array size
 * assert because many rows are purposely skipped. Not all IRQs are WKO IRQs,
 * so the IRQ index skips around. But, we still want the entire array to take
 * up the size of the total number of IRQs because the index to the array could
 * be any IRQ number.
 */
static const struct {
	uint8_t gpio_port;
	uint8_t gpio_mask;
	uint8_t wuc_group;
	uint8_t wuc_mask;
} gpio_irqs[] = {
	/*     irq           gpio_port,gpio_mask,wuc_group,wuc_mask */
	[IT83XX_IRQ_WKO20] =    {GPIO_D, (1<<0),        2, (1<<0)},
	[IT83XX_IRQ_WKO21] =    {GPIO_D, (1<<1),        2, (1<<1)},
	[IT83XX_IRQ_WKO22] =    {GPIO_C, (1<<4),        2, (1<<2)},
	[IT83XX_IRQ_WKO23] =    {GPIO_C, (1<<6),        2, (1<<3)},
	[IT83XX_IRQ_WKO24] =    {GPIO_D, (1<<2),        2, (1<<4)},
	[IT83XX_IRQ_WKO50] =    {GPIO_K, (1<<0),        5, (1<<0)},
	[IT83XX_IRQ_WKO51] =    {GPIO_K, (1<<1),        5, (1<<1)},
	[IT83XX_IRQ_WKO52] =    {GPIO_K, (1<<2),        5, (1<<2)},
	[IT83XX_IRQ_WKO53] =    {GPIO_K, (1<<3),        5, (1<<3)},
	[IT83XX_IRQ_WKO54] =    {GPIO_K, (1<<4),        5, (1<<4)},
	[IT83XX_IRQ_WKO55] =    {GPIO_K, (1<<5),        5, (1<<5)},
	[IT83XX_IRQ_WKO56] =    {GPIO_K, (1<<6),        5, (1<<6)},
	[IT83XX_IRQ_WKO57] =    {GPIO_K, (1<<7),        5, (1<<7)},
	[IT83XX_IRQ_WKO60] =    {GPIO_H, (1<<0),        6, (1<<0)},
	[IT83XX_IRQ_WKO61] =    {GPIO_H, (1<<1),        6, (1<<1)},
	[IT83XX_IRQ_WKO62] =    {GPIO_H, (1<<2),        6, (1<<2)},
	[IT83XX_IRQ_WKO63] =    {GPIO_H, (1<<3),        6, (1<<3)},
	[IT83XX_IRQ_WKO64] =    {GPIO_F, (1<<4),        6, (1<<4)},
	[IT83XX_IRQ_WKO65] =    {GPIO_F, (1<<5),        6, (1<<5)},
	[IT83XX_IRQ_WKO65] =    {GPIO_F, (1<<6),        6, (1<<6)},
	[IT83XX_IRQ_WKO67] =    {GPIO_F, (1<<7),        6, (1<<7)},
	[IT83XX_IRQ_WKO70] =    {GPIO_E, (1<<0),        7, (1<<0)},
	[IT83XX_IRQ_WKO71] =    {GPIO_E, (1<<1),        7, (1<<1)},
	[IT83XX_IRQ_WKO72] =    {GPIO_E, (1<<2),        7, (1<<2)},
	[IT83XX_IRQ_WKO73] =    {GPIO_E, (1<<3),        7, (1<<3)},
	[IT83XX_IRQ_WKO74] =    {GPIO_I, (1<<4),        7, (1<<4)},
	[IT83XX_IRQ_WKO75] =    {GPIO_I, (1<<5),        7, (1<<5)},
	[IT83XX_IRQ_WKO76] =    {GPIO_I, (1<<6),        7, (1<<6)},
	[IT83XX_IRQ_WKO77] =    {GPIO_I, (1<<7),        7, (1<<7)},
	[IT83XX_IRQ_WKO80] =    {GPIO_A, (1<<3),        8, (1<<0)},
	[IT83XX_IRQ_WKO81] =    {GPIO_A, (1<<4),        8, (1<<1)},
	[IT83XX_IRQ_WKO82] =    {GPIO_A, (1<<5),        8, (1<<2)},
	[IT83XX_IRQ_WKO83] =    {GPIO_A, (1<<6),        8, (1<<3)},
	[IT83XX_IRQ_WKO84] =    {GPIO_B, (1<<2),        8, (1<<4)},
	[IT83XX_IRQ_WKO85] =    {GPIO_C, (1<<0),        8, (1<<5)},
	[IT83XX_IRQ_WKO86] =    {GPIO_C, (1<<7),        8, (1<<6)},
	[IT83XX_IRQ_WKO87] =    {GPIO_D, (1<<7),        8, (1<<7)},
	[IT83XX_IRQ_WKO88] =    {GPIO_H, (1<<4),        9, (1<<0)},
	[IT83XX_IRQ_WKO89] =    {GPIO_H, (1<<5),        9, (1<<1)},
	[IT83XX_IRQ_WKO90] =    {GPIO_H, (1<<6),        9, (1<<2)},
	[IT83XX_IRQ_WKO91] =    {GPIO_A, (1<<0),        9, (1<<3)},
	[IT83XX_IRQ_WKO92] =    {GPIO_A, (1<<1),        9, (1<<4)},
	[IT83XX_IRQ_WKO93] =    {GPIO_A, (1<<2),        9, (1<<5)},
	[IT83XX_IRQ_WKO94] =    {GPIO_B, (1<<4),        9, (1<<6)},
	[IT83XX_IRQ_WKO95] =    {GPIO_C, (1<<2),        9, (1<<7)},
	[IT83XX_IRQ_WKO96] =    {GPIO_F, (1<<0),        10, (1<<0)},
	[IT83XX_IRQ_WKO97] =    {GPIO_F, (1<<1),        10, (1<<1)},
	[IT83XX_IRQ_WKO98] =    {GPIO_F, (1<<2),        10, (1<<2)},
	[IT83XX_IRQ_WKO99] =    {GPIO_F, (1<<3),        10, (1<<3)},
	[IT83XX_IRQ_WKO100] =   {GPIO_A, (1<<7),        10, (1<<4)},
	[IT83XX_IRQ_WKO101] =   {GPIO_B, (1<<0),        10, (1<<5)},
	[IT83XX_IRQ_WKO102] =   {GPIO_B, (1<<1),        10, (1<<6)},
	[IT83XX_IRQ_WKO103] =   {GPIO_B, (1<<3),        10, (1<<7)},
	[IT83XX_IRQ_WKO104] =   {GPIO_B, (1<<5),        11, (1<<0)},
	[IT83XX_IRQ_WKO105] =   {GPIO_B, (1<<6),        11, (1<<1)},
	[IT83XX_IRQ_WKO106] =   {GPIO_B, (1<<7),        11, (1<<2)},
	[IT83XX_IRQ_WKO107] =   {GPIO_C, (1<<1),        11, (1<<3)},
	[IT83XX_IRQ_WKO108] =   {GPIO_C, (1<<3),        11, (1<<4)},
	[IT83XX_IRQ_WKO109] =   {GPIO_C, (1<<5),        11, (1<<5)},
	[IT83XX_IRQ_WKO110] =   {GPIO_D, (1<<3),        11, (1<<6)},
	[IT83XX_IRQ_WKO111] =   {GPIO_D, (1<<4),        11, (1<<7)},
	[IT83XX_IRQ_WKO112] =   {GPIO_D, (1<<5),        12, (1<<0)},
	[IT83XX_IRQ_WKO113] =   {GPIO_D, (1<<6),        12, (1<<1)},
	[IT83XX_IRQ_WKO114] =   {GPIO_E, (1<<4),        12, (1<<2)},
	[IT83XX_IRQ_WKO115] =   {GPIO_G, (1<<0),        12, (1<<3)},
	[IT83XX_IRQ_WKO116] =   {GPIO_G, (1<<1),        12, (1<<4)},
	[IT83XX_IRQ_WKO117] =   {GPIO_G, (1<<2),        12, (1<<5)},
	[IT83XX_IRQ_WKO118] =   {GPIO_G, (1<<6),        12, (1<<6)},
	[IT83XX_IRQ_WKO119] =   {GPIO_I, (1<<0),        12, (1<<7)},
	[IT83XX_IRQ_WKO120] =   {GPIO_I, (1<<1),        13, (1<<0)},
	[IT83XX_IRQ_WKO121] =   {GPIO_I, (1<<2),        13, (1<<1)},
	[IT83XX_IRQ_WKO122] =   {GPIO_I, (1<<3),        13, (1<<2)},
	[IT83XX_IRQ_WKO128] =   {GPIO_J, (1<<0),        14, (1<<0)},
	[IT83XX_IRQ_WKO129] =   {GPIO_J, (1<<1),        14, (1<<1)},
	[IT83XX_IRQ_WKO130] =   {GPIO_J, (1<<2),        14, (1<<2)},
	[IT83XX_IRQ_WKO131] =   {GPIO_J, (1<<3),        14, (1<<3)},
	[IT83XX_IRQ_WKO132] =   {GPIO_J, (1<<4),        14, (1<<4)},
	[IT83XX_IRQ_WKO133] =   {GPIO_J, (1<<5),        14, (1<<5)},
	[IT83XX_IRQ_WKO136] =   {GPIO_L, (1<<0),        15, (1<<0)},
	[IT83XX_IRQ_WKO137] =   {GPIO_L, (1<<1),        15, (1<<1)},
	[IT83XX_IRQ_WKO138] =   {GPIO_L, (1<<2),        15, (1<<2)},
	[IT83XX_IRQ_WKO139] =   {GPIO_L, (1<<3),        15, (1<<3)},
	[IT83XX_IRQ_WKO140] =   {GPIO_L, (1<<4),        15, (1<<4)},
	[IT83XX_IRQ_WKO141] =   {GPIO_L, (1<<5),        15, (1<<5)},
	[IT83XX_IRQ_WKO142] =   {GPIO_L, (1<<6),        15, (1<<6)},
	[IT83XX_IRQ_WKO143] =   {GPIO_L, (1<<7),        15, (1<<7)},
	[IT83XX_IRQ_COUNT-1] =  {0,           0,         0,      0},
};
BUILD_ASSERT(ARRAY_SIZE(gpio_irqs) == IT83XX_IRQ_COUNT);

/**
 * Given a GPIO port and mask, find the corresponding WKO interrupt number.
 *
 * @param port  GPIO port
 * @param mask  GPIO mask
 *
 * @return IRQ for the WKO interrupt on the corresponding input pin.
 */
static int gpio_to_irq(uint8_t port, uint8_t mask)
{
	int i;

	for (i = 0; i < IT83XX_IRQ_COUNT; i++) {
		if (gpio_irqs[i].gpio_port == port &&
				gpio_irqs[i].gpio_mask == mask)
			return i;
	}

	return -1;
}

struct gpio_1p8v_t {
	uint8_t gpio_port;
	uint8_t gpio_mask;
	volatile uint8_t *ctrl_reg;
	uint8_t ctrl_mask;
};

const struct gpio_1p8v_t gpio_1p8v_ctrl[] = {
	{GPIO_A, (1 << 4), &IT83XX_GPIO_GRC24, (1 << 0)},
	{GPIO_A, (1 << 5), &IT83XX_GPIO_GRC24, (1 << 1)},
	{GPIO_B, (1 << 3), &IT83XX_GPIO_GRC22, (1 << 1)},
	{GPIO_B, (1 << 4), &IT83XX_GPIO_GRC22, (1 << 0)},
	{GPIO_B, (1 << 5), &IT83XX_GPIO_GRC19, (1 << 7)},
	{GPIO_B, (1 << 6), &IT83XX_GPIO_GRC19, (1 << 6)},
	{GPIO_C, (1 << 1), &IT83XX_GPIO_GRC19, (1 << 5)},
	{GPIO_C, (1 << 2), &IT83XX_GPIO_GRC19, (1 << 4)},
	{GPIO_C, (1 << 7), &IT83XX_GPIO_GRC19, (1 << 3)},
	{GPIO_D, (1 << 0), &IT83XX_GPIO_GRC19, (1 << 2)},
	{GPIO_D, (1 << 1), &IT83XX_GPIO_GRC19, (1 << 1)},
	{GPIO_D, (1 << 2), &IT83XX_GPIO_GRC19, (1 << 0)},
	{GPIO_D, (1 << 3), &IT83XX_GPIO_GRC20, (1 << 7)},
	{GPIO_D, (1 << 4), &IT83XX_GPIO_GRC20, (1 << 6)},
	{GPIO_E, (1 << 0), &IT83XX_GPIO_GRC20, (1 << 5)},
	{GPIO_E, (1 << 6), &IT83XX_GPIO_GRC20, (1 << 4)},
	{GPIO_E, (1 << 7), &IT83XX_GPIO_GRC20, (1 << 3)},
	{GPIO_F, (1 << 2), &IT83XX_GPIO_GRC20, (1 << 2)},
	{GPIO_F, (1 << 3), &IT83XX_GPIO_GRC20, (1 << 1)},
	{GPIO_F, (1 << 4), &IT83XX_GPIO_GRC20, (1 << 0)},
	{GPIO_F, (1 << 5), &IT83XX_GPIO_GRC21, (1 << 7)},
	{GPIO_F, (1 << 6), &IT83XX_GPIO_GRC21, (1 << 6)},
	{GPIO_F, (1 << 7), &IT83XX_GPIO_GRC21, (1 << 5)},
	{GPIO_H, (1 << 0), &IT83XX_GPIO_GRC21, (1 << 2)},
	{GPIO_H, (1 << 1), &IT83XX_GPIO_GRC21, (1 << 1)},
	{GPIO_H, (1 << 2), &IT83XX_GPIO_GRC21, (1 << 0)},
	{GPIO_I, (1 << 1), &IT83XX_GPIO_GRC23, (1 << 4)},
	{GPIO_I, (1 << 2), &IT83XX_GPIO_GRC23, (1 << 5)},
	{GPIO_I, (1 << 3), &IT83XX_GPIO_GRC23, (1 << 6)},
	{GPIO_I, (1 << 4), &IT83XX_GPIO_GRC23, (1 << 7)},
	{GPIO_J, (1 << 0), &IT83XX_GPIO_GRC23, (1 << 0)},
	{GPIO_J, (1 << 1), &IT83XX_GPIO_GRC23, (1 << 1)},
	{GPIO_J, (1 << 2), &IT83XX_GPIO_GRC23, (1 << 2)},
	{GPIO_J, (1 << 3), &IT83XX_GPIO_GRC23, (1 << 3)},
};

static void gpio_1p8v_3p3v_sel(uint8_t port, uint8_t mask, uint32_t flags)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gpio_1p8v_ctrl); i++) {
		if (gpio_1p8v_ctrl[i].gpio_port == port &&
				gpio_1p8v_ctrl[i].gpio_mask == mask) {
			if (flags & GPIO_SEL_1P8V)
				*gpio_1p8v_ctrl[i].ctrl_reg |=
					gpio_1p8v_ctrl[i].ctrl_mask;
			else
				*gpio_1p8v_ctrl[i].ctrl_reg &=
					~gpio_1p8v_ctrl[i].ctrl_mask;
			break;
		}
	}
}

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	uint32_t pin = 0;

	/* For each bit high in the mask, set that pin to use alt. func. */
	while (mask > 0) {
		/*
		 * If func is non-negative, set for alternate function.
		 * Otherwise, turn the pin into an input as it's default.
		 */
		if ((mask & 1) && func >= 0)
			IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) &= ~0xc0;
		else if ((mask & 1) && func < 0)
			IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) =
			(IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) | 0x80) & ~0x40;

		pin++;
		mask >>= 1;
	}
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return (IT83XX_GPIO_DATA(gpio_list[signal].port) &
			gpio_list[signal].mask) ? 1 : 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	uint32_t int_mask = get_int_mask();

	/* critical section with interrupts off */
	interrupt_disable();
	if (value)
		IT83XX_GPIO_DATA(gpio_list[signal].port) |=
				 gpio_list[signal].mask;
	else
		IT83XX_GPIO_DATA(gpio_list[signal].port) &=
				~gpio_list[signal].mask;
	/* restore interrupts */
	set_int_mask(int_mask);
}

void gpio_kbs_pin_gpio_mode(uint32_t port, uint32_t mask, uint32_t flags)
{
	if (port == GPIO_KSO_H)
		IT83XX_KBS_KSOHGCTRL |= mask;
	else if (port == GPIO_KSO_L)
		IT83XX_KBS_KSOLGCTRL |= mask;
	else if (port == GPIO_KSI)
		IT83XX_KBS_KSIGCTRL |= mask;
}

/* Returns true when the falling trigger bit actually mean both trigger. */
static int group_falling_is_both(const int group)
{
	return group == 7 || group == 10 || group == 12;
}

static const char *get_gpio_string(const int port, const int mask)
{
	static char buffer[3];
	int i;

	buffer[0] = port - GPIO_A + 'A';
	buffer[1] = '!';

	for (i = 0; i < 8; ++i) {
		if (mask & (1 << i)) {
			buffer[1] = i + '0';
			break;
		}
	}
	return buffer;
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	uint32_t pin = 0;
	uint32_t mask_copy = mask;

	/*
	 * Select open drain first, so that we don't glitch the signal
	 * when changing the line to an output.
	 */
	if (flags & GPIO_OPEN_DRAIN)
		IT83XX_GPIO_GPOT(port) |= mask;
	else
		IT83XX_GPIO_GPOT(port) &= ~mask;

	/* To select 1.8v or 3.3v support. */
	gpio_1p8v_3p3v_sel(port, mask, flags);

	/* If output, set level before changing type to an output. */
	if (flags & GPIO_OUTPUT) {
		if (flags & GPIO_HIGH)
			IT83XX_GPIO_DATA(port) |= mask;
		else if (flags & GPIO_LOW)
			IT83XX_GPIO_DATA(port) &= ~mask;
	}

	/* For each bit high in the mask, set input/output and pullup/down. */
	while (mask_copy > 0) {
		if (mask_copy & 1) {
			/* Set input or output. */
			if (flags & GPIO_OUTPUT)
				IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) =
				(IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) | 0x40)
				& ~0x80;
			else
				IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) =
				(IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) | 0x80)
				& ~0x40;

			/* Handle pullup / pulldown */
			if (flags & GPIO_PULL_UP) {
				IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) =
				(IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) | 0x04)
				& ~0x02;
			} else if (flags & GPIO_PULL_DOWN) {
				IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) =
				(IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) | 0x02)
				& ~0x04;
			} else {
				/* No pull up/down */
				IT83XX_GPIO_CTRL(CTRL_BASE(port), pin) &= ~0x06;
			}
		}

		pin++;
		mask_copy >>= 1;
	}

	if (flags & (GPIO_INT_F_RISING | GPIO_INT_F_FALLING)) {
		int irq, wuc_group, wuc_mask;
		irq = gpio_to_irq(port, mask);
		wuc_group = gpio_irqs[irq].wuc_group;
		wuc_mask = gpio_irqs[irq].wuc_mask;

		if (flags & GPIO_INT_F_FALLING) {
			if (!!(flags & GPIO_INT_F_RISING) !=
			    group_falling_is_both(wuc_group)) {
				ccprintf("!!Fix GPIO %s interrupt config!!\n",
					 get_gpio_string(port, mask));
			}
			*(wuemr(wuc_group)) |= wuc_mask;
		} else {
			*(wuemr(wuc_group)) &= ~wuc_mask;
		}
	}
}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	int irq = gpio_to_irq(gpio_list[signal].port, gpio_list[signal].mask);

	if (irq == -1)
		return EC_ERROR_UNKNOWN;
	else
		task_enable_irq(irq);

	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	int irq = gpio_to_irq(gpio_list[signal].port, gpio_list[signal].mask);

	if (irq == -1)
		return EC_ERROR_UNKNOWN;
	else
		task_disable_irq(irq);

	return EC_SUCCESS;
}

int gpio_clear_pending_interrupt(enum gpio_signal signal)
{
	int irq = gpio_to_irq(gpio_list[signal].port, gpio_list[signal].mask);

	if (irq == -1)
		return EC_ERROR_UNKNOWN;

	*(wuesr(gpio_irqs[irq].wuc_group)) = gpio_irqs[irq].wuc_mask;
	task_clear_pending_irq(irq);
	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int is_warm = system_is_reboot_warm();
	int flags;
	int i;

	IT83XX_GPIO_GCR = 0x06;

#ifndef CONFIG_USB_PD_TCPM_ITE83XX
	/* To prevent cc pins leakage if we don't use pd module */
	for (i = 0; i < USBPD_PORT_COUNT; i++) {
		IT83XX_USBPD_CCGCR(i) = 0x1f;
		/*
		 * bit7 and bit3: Dis-connect CC with UP/RD/DET/TX/RX.
		 * bit6 and bit2: Dis-connect CC with 5.1K resister to GND.
		 * bit5 and bit1: Disable CC voltage detector.
		 * bit4 and bit0: Disable CC.
		 */
		IT83XX_USBPD_CCCSR(i) = 0xff;
		IT83XX_USBPD_CCPSR(i) = 0x66;
	}
#endif

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

		if (g->port > GPIO_KBS_OFF)
			/* KSO/KSI pins to GPIO mode (input only). */
			gpio_kbs_pin_gpio_mode(g->port, g->mask, flags);
		else
			/* Set up GPIO based on flags */
			gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}

/**
 * Handle a GPIO interrupt by calling the pins corresponding handler if
 * one exists.
 *
 * @param port		GPIO port (GPIO_*)
 * @param mask		GPIO mask
 */
static void gpio_interrupt(int port, uint8_t mask)
{
	int i = 0;
	const struct gpio_info *g = gpio_list;

	for (i = 0; i < GPIO_IH_COUNT; i++, g++) {
		if (port == g->port && (mask & g->mask)) {
			gpio_irq_handlers[i](i);
			return;
		}
	}
}

/**
 * Define one IRQ function to handle all GPIO interrupts. The IRQ determines
 * the interrupt number which was triggered, calls the master handler above,
 * and clears status registers.
 */
static void __gpio_irq(void)
{
	/* Determine interrupt number. */
	int irq = intc_get_ec_int();

#ifdef HAS_TASK_KEYSCAN
	if (irq == IT83XX_IRQ_WKINTC) {
		keyboard_raw_interrupt();
		return;
	}
#endif

#ifdef CONFIG_HOSTCMD_X86
	if (irq == IT83XX_IRQ_WKINTAD)
		return;
#endif

	/*
	 * Clear the WUC status register. Note the external pin first goes
	 * to the WUC module and is always edge triggered.
	 */
	*(wuesr(gpio_irqs[irq].wuc_group)) = gpio_irqs[irq].wuc_mask;

	/*
	 * Clear the interrupt controller  status register. Note the interrupt
	 * controller is level triggered from the WUC status.
	 */
	task_clear_pending_irq(irq);

	/* Run the GPIO master handler above with corresponding port/mask. */
	gpio_interrupt(gpio_irqs[irq].gpio_port, gpio_irqs[irq].gpio_mask);
}

/* Route all WKO interrupts coming from INT#2 into __gpio_irq. */
DECLARE_IRQ(CPU_INT_2_ALL_GPIOS, __gpio_irq, 1);
