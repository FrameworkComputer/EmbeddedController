/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "cpu.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "version.h"


static void check_reset_cause(void)
{
	enum system_reset_cause_t reset_cause = SYSTEM_RESET_UNKNOWN;
	uint32_t raw_cause = STM32_RCC_CSR;

	/* Clear the hardware reset cause by setting the RMVF bit */
	STM32_RCC_CSR |= 1 << 24;

	if (raw_cause & 0x60000000) {
		/* IWDG pr WWDG */
		reset_cause = SYSTEM_RESET_WATCHDOG;
	} else if (raw_cause & 0x10000000) {
		reset_cause = SYSTEM_RESET_SOFT;
	} else if (raw_cause & 0x08000000) {
		reset_cause = SYSTEM_RESET_POWER_ON;
	} else if (raw_cause & 0x04000000) {
		reset_cause = SYSTEM_RESET_RESET_PIN;
	} else if (raw_cause & 0xFE000000) {
		reset_cause = SYSTEM_RESET_OTHER;
	}
	system_set_reset_cause(reset_cause);
}


void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* we are going to hibernate ... */
	while (1)
		/* NOT IMPLEMENTED */;
}


int system_pre_init(void)
{
	/* enable clock on Power module */
	STM32_RCC_APB1ENR |= 1 << 28;
	/* Enable access to RCC CSR register and RTC backup registers */
	STM32_PWR_CR |= 1 << 8;

	/* switch on LSI */
	STM32_RCC_CSR |= 1 << 0;
	/* Wait for LSI to be ready */
	while (!(STM32_RCC_CSR & (1 << 1)))
		;
	/* re-configure RTC if needed */
#if defined(CHIP_VARIANT_stm32l15x)
	if ((STM32_RCC_CSR & 0x00C30000) != 0x00420000) {
		/* the RTC settings are bad, we need to reset it */
		STM32_RCC_CSR |= 0x00800000;
		/* Enable RTC and use LSI as clock source */
		STM32_RCC_CSR = (STM32_RCC_CSR & ~0x00C30000) | 0x00420000;
	}
#elif defined(CHIP_VARIANT_stm32f100)
	if ((STM32_RCC_BDCR & 0x00018300) != 0x00008200) {
		/* the RTC settings are bad, we need to reset it */
		STM32_RCC_BDCR |= 0x00010000;
		/* Enable RTC and use LSI as clock source */
		STM32_RCC_BDCR = (STM32_RCC_BDCR & ~0x00018300) | 0x00008200;
	}
#else
#error "Unsupported chip variant"
#endif

	check_reset_cause();

	return EC_SUCCESS;
}


void system_reset(int is_hard)
{
	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* TODO: (crosbug.com/p/7470) support hard boot; this is a
	 * soft boot. */
	CPU_NVIC_APINT = 0x05fa0004;

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}


int system_set_scratchpad(uint32_t value)
{
	STM32_RTC_BACKUP(0) = value;

	return EC_SUCCESS;
}


uint32_t system_get_scratchpad(void)
{
	return STM32_RTC_BACKUP(0);
}


const char *system_get_chip_vendor(void)
{
	return "stm";
}

const char *system_get_chip_name(void)
{
	return STRINGIFY(CHIP_VARIANT);
}

const char *system_get_chip_revision(void)
{
	return "";
}
