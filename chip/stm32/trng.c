/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware Random Number Generator */

#include "common.h"
#include "panic.h"
#include "registers.h"
#include "task.h"
#include "trng.h"
#include "util.h"

uint32_t rand(void)
{
	int tries = 40;
	/* Wait for a valid random number */
	while (!(STM32_RNG_SR & STM32_RNG_SR_DRDY) && --tries)
		;
	/* we cannot afford to feed the caller with a dummy number */
	if (!tries)
		software_panic(PANIC_SW_BAD_RNG, task_get_current());
	/* Finally the 32-bit of entropy */
	return STM32_RNG_DR;
}

void rand_bytes(void *buffer, size_t len)
{
	while (len) {
		uint32_t number = rand();
		size_t cnt = 4;
		/* deal with the lack of alignment guarantee in the API */
		uintptr_t align = (uintptr_t)buffer & 3;

		if (len < 4 || align) {
			cnt = MIN(4 - align, len);
			memcpy(buffer, &number, cnt);
		} else {
			*(uint32_t *)buffer = number;
		}
		len -= cnt;
		buffer += cnt;
	}
}

void init_trng(void)
{
	/* Enable the 48Mhz internal RC oscillator */
	STM32_RCC_CRRCR |= STM32_RCC_CRRCR_HSI48ON;
	/* no timeout: we watchdog if the oscillator doesn't start */
	while (!(STM32_RCC_CRRCR & STM32_RCC_CRRCR_HSI48RDY))
		;

	/* Clock the TRNG using the HSI48 */
	STM32_RCC_CCIPR = (STM32_RCC_CCIPR & ~STM32_RCC_CCIPR_CLK48SEL_MASK)
			| (0 << STM32_RCC_CCIPR_CLK48SEL_SHIFT);

	/* Enable the RNG logic */
	STM32_RCC_AHB2ENR |= STM32_RCC_AHB2ENR_RNGEN;
	/* Start the random number generation */
	STM32_RNG_CR |= STM32_RNG_CR_RNGEN;
}

void exit_trng(void)
{
	STM32_RNG_CR &= ~STM32_RNG_CR_RNGEN;
	STM32_RCC_AHB2ENR &= ~STM32_RCC_AHB2ENR_RNGEN;
	STM32_RCC_CRRCR &= ~STM32_RCC_CRRCR_HSI48ON;
}
