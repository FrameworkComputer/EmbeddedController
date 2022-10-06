/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * GPIO module for Chrome EC
 *
 * These functions are shared by the STM32F0 and STM32L variants.
 */

#include "builtin/assert.h"
#include "common.h"
#include "gpio_chip.h"
#include "registers.h"
#include "util.h"

static uint32_t expand_to_2bit_mask(uint32_t mask)
{
	uint32_t mask_out = 0;
	while (mask) {
		int bit = get_next_bit(&mask);
		mask_out |= 3 << (bit * 2);
	}
	return mask_out;
}

int gpio_get_flags_by_mask(uint32_t port, uint32_t mask)
{
	uint32_t flags = 0;
	uint32_t val = 0;
	const uint32_t mask2 = expand_to_2bit_mask(mask);

	/* Only one bit must be set. */
	if ((mask != (mask & -mask)) || (mask == 0))
		return 0;

	/* Check output type. */
	val = STM32_GPIO_PUPDR(port) & mask2;
	if (val == (0x55555555 & mask2))
		flags |= GPIO_PULL_UP;
	if (val == (0xaaaaaaaa & mask2))
		flags |= GPIO_PULL_DOWN;

	if (STM32_GPIO_OTYPER(port) & mask)
		flags |= GPIO_OPEN_DRAIN;

	/* Check mode. */
	val = STM32_GPIO_MODER(port) & mask2;
	if (val == (0x55555555 & mask2))
		flags |= GPIO_OUTPUT;
	if (val == (0xFFFFFFFF & mask2))
		flags |= GPIO_ANALOG;
	if (val == (0x0 & mask2))
		flags |= GPIO_INPUT;
	if (val == (0xaaaaaaaa & mask2))
		flags |= GPIO_ALTERNATE;

	if (flags & GPIO_OUTPUT) {
		if (STM32_GPIO_ODR(port) & mask)
			flags |= GPIO_HIGH;
		else
			flags |= GPIO_LOW;
	}

	if (STM32_EXTI_RTSR & mask)
		flags |= GPIO_INT_F_RISING;
	if (STM32_EXTI_RTSR & mask)
		flags |= GPIO_INT_F_RISING;

	return flags;
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	/* Bitmask for registers with 2 bits per GPIO pin */
	const uint32_t mask2 = expand_to_2bit_mask(mask);
	uint32_t val;

	/* Set up pullup / pulldown */
	val = STM32_GPIO_PUPDR(port) & ~mask2;
	if (flags & GPIO_PULL_UP)
		val |= 0x55555555 & mask2; /* Pull Up = 01 */
	else if (flags & GPIO_PULL_DOWN)
		val |= 0xaaaaaaaa & mask2; /* Pull Down = 10 */
	STM32_GPIO_PUPDR(port) = val;

	/*
	 * Select open drain first, so that we don't glitch the signal when
	 * changing the line to an output.
	 */
	if (flags & GPIO_OPEN_DRAIN)
		STM32_GPIO_OTYPER(port) |= mask;
	else
		STM32_GPIO_OTYPER(port) &= ~mask;

	val = STM32_GPIO_MODER(port) & ~mask2;
	if (flags & GPIO_OUTPUT) {
		/*
		 * Set pin level first to avoid glitching.  This is harmless on
		 * STM32L because the set/reset register isn't connected to the
		 * output drivers until the pin is made an output.
		 */
		if (flags & GPIO_HIGH)
			STM32_GPIO_BSRR(port) = mask;
		else if (flags & GPIO_LOW)
			STM32_GPIO_BSRR(port) = mask << 16;

		/* General purpose, MODE = 01 */
		val |= 0x55555555 & mask2;
		STM32_GPIO_MODER(port) = val;

	} else if (flags & GPIO_ANALOG) {
		/* Analog, MODE=11 */
		val |= 0xFFFFFFFF & mask2;
		STM32_GPIO_MODER(port) = val;
	} else if (flags & GPIO_INPUT) {
		/* Input, MODE=00 */
		STM32_GPIO_MODER(port) = val;
	} else if (flags & GPIO_ALTERNATE) {
		/* Alternate, MODE=10 */
		val |= 0xaaaaaaaa & mask2;
		STM32_GPIO_MODER(port) = val;
	}

	/* Set up interrupts if necessary */
	ASSERT(!(flags & (GPIO_INT_F_LOW | GPIO_INT_F_HIGH)));
	if (flags & GPIO_INT_F_RISING)
		STM32_EXTI_RTSR |= mask;
	if (flags & GPIO_INT_F_FALLING)
		STM32_EXTI_FTSR |= mask;
	/* Interrupt is enabled by gpio_enable_interrupt() */
}

void gpio_set_alternate_function(uint32_t port, uint32_t mask,
				 enum gpio_alternate_func func)
{
	/* Ensure that the func parameter isn't overflowed */
	BUILD_ASSERT((int)MODULE_COUNT <= (int)GPIO_ALT_FUNC_MAX);

	int bit;
	uint32_t half;
	uint32_t afr;
	uint32_t moder = STM32_GPIO_MODER(port);

	if (func == GPIO_ALT_FUNC_NONE) {
		/* Return to normal GPIO function, defaulting to input. */
		while (mask) {
			bit = get_next_bit(&mask);
			moder &= ~(0x3 << (bit * 2));
		}
		STM32_GPIO_MODER(port) = moder;
		return;
	}

	/* Low half of the GPIO bank */
	half = mask & 0xff;
	afr = STM32_GPIO_AFRL(port);
	while (half) {
		bit = get_next_bit(&half);
		afr &= ~(0xf << (bit * 4));
		afr |= func << (bit * 4);
		moder &= ~(0x3 << (bit * 2 + 0));
		moder |= 0x2 << (bit * 2 + 0);
	}
	STM32_GPIO_AFRL(port) = afr;

	/* High half of the GPIO bank */
	half = (mask >> 8) & 0xff;
	afr = STM32_GPIO_AFRH(port);
	while (half) {
		bit = get_next_bit(&half);
		afr &= ~(0xf << (bit * 4));
		afr |= func << (bit * 4);
		moder &= ~(0x3 << (bit * 2 + 16));
		moder |= 0x2 << (bit * 2 + 16);
	}
	STM32_GPIO_AFRH(port) = afr;
	STM32_GPIO_MODER(port) = moder;
}
