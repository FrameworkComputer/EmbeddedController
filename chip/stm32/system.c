/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "console.h"
#include "cpu.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "version.h"
#include "watchdog.h"

#define CONSOLE_BIT_MASK 0x8000

enum bkpdata_index {
	BKPDATA_INDEX_SCRATCHPAD,	/* General-purpose scratchpad */
	BKPDATA_INDEX_SAVED_RESET_FLAGS,/* Saved reset flags */
	BKPDATA_INDEX_VBNV_CONTEXT0,
	BKPDATA_INDEX_VBNV_CONTEXT1,
	BKPDATA_INDEX_VBNV_CONTEXT2,
	BKPDATA_INDEX_VBNV_CONTEXT3,
	BKPDATA_INDEX_VBNV_CONTEXT4,
	BKPDATA_INDEX_VBNV_CONTEXT5,
	BKPDATA_INDEX_VBNV_CONTEXT6,
	BKPDATA_INDEX_VBNV_CONTEXT7,
};

/**
 * Read backup register at specified index.
 *
 * @return The value of the register or 0 if invalid index.
 */
static uint16_t bkpdata_read(enum bkpdata_index index)
{
	if (index < 0 || index >= STM32_BKP_ENTRIES)
		return 0;

	return STM32_BKP_DATA(index);
}

/**
 * Write hibernate register at specified index.
 *
 * @return nonzero if error.
 */
static int bkpdata_write(enum bkpdata_index index, uint16_t value)
{
	if (index < 0 || index >= STM32_BKP_ENTRIES)
		return EC_ERROR_INVAL;

	STM32_BKP_DATA(index) = value;
	return EC_SUCCESS;
}

static void check_reset_cause(void)
{
	uint32_t flags = bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS);
	uint32_t raw_cause = STM32_RCC_CSR;
	uint32_t pwr_status = STM32_PWR_CSR;

	uint32_t console_en = flags & CONSOLE_BIT_MASK;
	flags &= ~CONSOLE_BIT_MASK;

	/* Clear the hardware reset cause by setting the RMVF bit */
	STM32_RCC_CSR |= 1 << 24;
	/* Clear SBF in PWR_CSR */
	STM32_PWR_CR |= 1 << 3;
	/* Clear saved reset flags */
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, 0 | console_en);

	if (raw_cause & 0x60000000) {
		/*
		 * IWDG or WWDG, if the watchdog was not used as an hard reset
		 * mechanism
		 */
		if (!(flags & RESET_FLAG_HARD))
			flags |= RESET_FLAG_WATCHDOG;
	}

	if (raw_cause & 0x10000000)
		flags |= RESET_FLAG_SOFT;

	if (raw_cause & 0x08000000)
		flags |= RESET_FLAG_POWER_ON;

	if (raw_cause & 0x04000000)
		flags |= RESET_FLAG_RESET_PIN;

	if (pwr_status & 0x00000002)
		/* Hibernated and subsequently awakened */
		flags |= RESET_FLAG_HIBERNATE;

	if (!flags && (raw_cause & 0xfe000000))
		flags |= RESET_FLAG_OTHER;

	system_set_reset_flags(flags);
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/*
	 * TODO: implement hibernate.
	 *
	 * Until then, treat this as a request to hard-reboot.
	 */
	cprintf(CC_SYSTEM, "[%T hibernate not supported, so rebooting]\n");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
}

void system_pre_init(void)
{
	/* enable clock on Power module */
	STM32_RCC_APB1ENR |= 1 << 28;
	/* enable backup registers */
	STM32_RCC_APB1ENR |= 1 << 27;
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
#elif defined(CHIP_VARIANT_stm32f100) || defined(CHIP_VARIANT_stm32f10x)
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
}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	uint32_t console_en = bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS) &
			      CONSOLE_BIT_MASK;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/*
	 * TODO: Check if a collision between reset flags and fake wp occurred.
	 * Remove this when we have real write protect pin.
	 */
	ASSERT(!(system_get_reset_flags() & 0x8000));

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	/* Remember that the software asked us to hard reboot */
	if (flags & SYSTEM_RESET_HARD)
		save_flags |= RESET_FLAG_HARD;

	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, save_flags | console_en);

	if (flags & SYSTEM_RESET_HARD) {
		/* Ask the watchdog to trigger a hard reboot */
		STM32_IWDG_KR = 0x5555;
		STM32_IWDG_RLR = 0x1;
		STM32_IWDG_KR = 0xcccc;
		/* wait for the watchdog */
		while (1)
			;
	} else {
		CPU_NVIC_APINT = 0x05fa0004;
	}

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

int system_set_scratchpad(uint32_t value)
{
	/* Check if value fits in 16 bits */
	if (value & 0xffff0000)
		return EC_ERROR_INVAL;
	return bkpdata_write(BKPDATA_INDEX_SCRATCHPAD, (uint16_t)value);
}

uint32_t system_get_scratchpad(void)
{
	return (uint32_t)bkpdata_read(BKPDATA_INDEX_SCRATCHPAD);
}

const char *system_get_chip_vendor(void)
{
	return "stm";
}

const char *system_get_chip_name(void)
{
	if (system_get_console_force_enabled())
		return STRINGIFY(CHIP_VARIANT-unsafe);
	else
		return STRINGIFY(CHIP_VARIANT);
}

const char *system_get_chip_revision(void)
{
	return "";
}

int system_get_vbnvcontext(uint8_t *block)
{
	enum bkpdata_index i;
	uint16_t value;

	for (i = BKPDATA_INDEX_VBNV_CONTEXT0;
			i <= BKPDATA_INDEX_VBNV_CONTEXT7; i++) {
		value = bkpdata_read(i);
		*block++ = (uint8_t)(value & 0xff);
		*block++ = (uint8_t)(value >> 8);
	}

	return EC_SUCCESS;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	enum bkpdata_index i;
	uint16_t value;
	int err;

	for (i = BKPDATA_INDEX_VBNV_CONTEXT0;
			i <= BKPDATA_INDEX_VBNV_CONTEXT7; i++) {
		value = *block++;
		value |= ((uint16_t)*block++) << 8;
		err = bkpdata_write(i, value);
		if (err)
			return err;
	}

	return EC_SUCCESS;
}

int system_set_console_force_enabled(int val)
{
	uint16_t flags = bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS);

	if (val)
		flags |= CONSOLE_BIT_MASK;
	else
		flags &= ~CONSOLE_BIT_MASK;

	return bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, flags);
}

int system_get_console_force_enabled(void)
{
	if (bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS) & CONSOLE_BIT_MASK)
		return 1;
	else
		return 0;
}
