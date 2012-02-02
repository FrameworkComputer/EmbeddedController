/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "cpu.h"
#include "registers.h"
#include "system.h"


static void check_reset_cause(void)
{
	enum system_image_copy_t copy = system_get_image_copy();
	enum system_reset_cause_t reset_cause = SYSTEM_RESET_UNKNOWN;
	uint32_t raw_cause = STM32L_RCC_CSR;

	if (copy == SYSTEM_IMAGE_RW_A || copy == SYSTEM_IMAGE_RW_B) {
		/* If we're in image A or B, the only way we can get there is
		 * via a warm reset. */
		reset_cause = SYSTEM_RESET_SOFT_WARM;
	} else if (raw_cause & 0x60000000) {
		/* IWDG pr WWDG */
		reset_cause = SYSTEM_RESET_WATCHDOG;
	} else if (raw_cause & 0x10000000) {
		reset_cause = SYSTEM_RESET_SOFT_COLD;
	} else if (raw_cause & 0x08000000) {
		reset_cause = SYSTEM_RESET_POWER_ON;
	} else if (raw_cause & 0x04000000) {
		reset_cause = SYSTEM_RESET_RESET_PIN;
	} else if (raw_cause & 0xFE000000) {
		reset_cause = SYSTEM_RESET_OTHER;
	} else {
		reset_cause = SYSTEM_RESET_UNKNOWN;
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
	STM32L_RCC_APB1ENR |= 1 << 28;
	/* Enable access to RCC CSR register and RTC backup registers */
	STM32L_PWR_CR |= 1 << 8;

	/* switch on LSI */
	STM32L_RCC_CSR |= 1 << 0;
	/* Wait for LSI to be ready */
	while (!(STM32L_RCC_CSR & (1 << 1)))
		;
	/* re-configure RTC if needed */
	if ((STM32L_RCC_CSR & 0x00C30000) != 0x00420000) {
		/* the RTC settings are bad, we need to reset it */
		STM32L_RCC_CSR |= 0x00800000;
		/* Enable RTC and use LSI as clock source */
		STM32L_RCC_CSR = (STM32L_RCC_CSR & ~0x00C30000) | 0x00420000;
	}

	check_reset_cause();

	return EC_SUCCESS;
}


int system_init(void)
{
	/* Clear the hardware reset cause by setting the RMVF bit,
	 * now that we've committed to running this image.
	 */
	STM32L_RCC_CSR |= 1 << 24;

	return EC_SUCCESS;
}


int system_reset(int is_cold)
{
	/* TODO: (crosbug.com/p/7470) support cold boot; this is a
	   warm boot. */
	CPU_NVIC_APINT = 0x05fa0004;

	/* Spin and wait for reboot; should never return */
	/* TODO: (crosbug.com/p/7471) should disable task swaps while
	   waiting */
	while (1)
		;

	return EC_ERROR_UNKNOWN;
}


int system_set_scratchpad(uint32_t value)
{
	STM32L_RTC_BACKUP(0) = value;

	return EC_SUCCESS;
}


uint32_t system_get_scratchpad(void)
{
	return STM32L_RTC_BACKUP(0);
}
