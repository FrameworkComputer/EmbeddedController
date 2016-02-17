/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "clock.h"
#include "console.h"
#include "cpu.h"
#include "flash.h"
#include "host_command.h"
#include "registers.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "version.h"
#include "watchdog.h"

#define CONSOLE_BIT_MASK 0x8000

enum bkpdata_index {
	BKPDATA_INDEX_SCRATCHPAD,	     /* General-purpose scratchpad */
	BKPDATA_INDEX_SAVED_RESET_FLAGS,     /* Saved reset flags */
	BKPDATA_INDEX_VBNV_CONTEXT0,
	BKPDATA_INDEX_VBNV_CONTEXT1,
	BKPDATA_INDEX_VBNV_CONTEXT2,
	BKPDATA_INDEX_VBNV_CONTEXT3,
	BKPDATA_INDEX_VBNV_CONTEXT4,
	BKPDATA_INDEX_VBNV_CONTEXT5,
	BKPDATA_INDEX_VBNV_CONTEXT6,
	BKPDATA_INDEX_VBNV_CONTEXT7,
#ifdef CONFIG_SOFTWARE_PANIC
	BKPDATA_INDEX_SAVED_PANIC_REASON,    /* Saved panic reason */
	BKPDATA_INDEX_SAVED_PANIC_INFO,      /* Saved panic data */
	BKPDATA_INDEX_SAVED_PANIC_EXCEPTION, /* Saved panic exception code */
#endif
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

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32F0) || \
	defined(CHIP_FAMILY_STM32F3)
	if (index & 1)
		return STM32_BKP_DATA(index >> 1) >> 16;
	else
		return STM32_BKP_DATA(index >> 1) & 0xFFFF;
#else
	return STM32_BKP_DATA(index);
#endif
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

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32F0) || \
	defined(CHIP_FAMILY_STM32F3)
	if (index & 1) {
		uint32_t val = STM32_BKP_DATA(index >> 1);
		val = (val & 0x0000FFFF) | (value << 16);
		STM32_BKP_DATA(index >> 1) = val;
	} else {
		uint32_t val = STM32_BKP_DATA(index >> 1);
		val = (val & 0xFFFF0000) | value;
		STM32_BKP_DATA(index >> 1) = val;
	}
#else
	STM32_BKP_DATA(index) = value;
#endif
	return EC_SUCCESS;
}

void __no_hibernate(uint32_t seconds, uint32_t microseconds)
{
#ifdef CONFIG_COMMON_RUNTIME
	/*
	 * Hibernate not implemented on this platform.
	 *
	 * Until then, treat this as a request to hard-reboot.
	 */
	cprints(CC_SYSTEM, "hibernate not supported, so rebooting");
	cflush();
	system_reset(SYSTEM_RESET_HARD);
#endif
}

void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
	__attribute__((weak, alias("__no_hibernate")));

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
#ifdef CONFIG_HOSTCMD_PD
	/* Inform the PD MCU that we are going to hibernate. */
	host_command_pd_request_hibernate();
	/* Wait to ensure exchange with PD before hibernating. */
	msleep(100);
#endif

	/* Flush console before hibernating */
	cflush();

	if (board_hibernate)
		board_hibernate();

	/* chip specific standby mode */
	__enter_hibernate(seconds, microseconds);
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

	/*
	 * WORKAROUND: as we cannot de-activate the watchdog during
	 * long hibernation, we are woken-up once by the watchdog and
	 * go back to hibernate if we detect that condition, without
	 * watchdog initialized this time.
	 * The RTC deadline (if any) is already set.
	 */
	if ((flags & (RESET_FLAG_HIBERNATE | RESET_FLAG_WATCHDOG)) ==
		     (RESET_FLAG_HIBERNATE | RESET_FLAG_WATCHDOG)) {
		__enter_hibernate(0, 0);
	}

	system_set_reset_flags(flags);
}

void system_pre_init(void)
{
#ifdef CONFIG_SOFTWARE_PANIC
	uint16_t reason, info;
	uint8_t exception;
#endif

	/* enable clock on Power module */
	STM32_RCC_APB1ENR |= 1 << 28;
	/* enable backup registers */
	STM32_RCC_APB1ENR |= 1 << 27;
	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
	/* Enable access to RCC CSR register and RTC backup registers */
	STM32_PWR_CR |= 1 << 8;

	/* switch on LSI */
	STM32_RCC_CSR |= 1 << 0;
	/* Wait for LSI to be ready */
	while (!(STM32_RCC_CSR & (1 << 1)))
		;
	/* re-configure RTC if needed */
#ifdef CHIP_FAMILY_STM32L
	if ((STM32_RCC_CSR & 0x00C30000) != 0x00420000) {
		/* the RTC settings are bad, we need to reset it */
		STM32_RCC_CSR |= 0x00800000;
		/* Enable RTC and use LSI as clock source */
		STM32_RCC_CSR = (STM32_RCC_CSR & ~0x00C30000) | 0x00420000;
	}
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	if ((STM32_RCC_BDCR & 0x00018300) != 0x00008200) {
		/* the RTC settings are bad, we need to reset it */
		STM32_RCC_BDCR |= 0x00010000;
		/* Enable RTC and use LSI as clock source */
		STM32_RCC_BDCR = (STM32_RCC_BDCR & ~0x00018300) | 0x00008200;
	}
#else
#error "Unsupported chip family"
#endif

	check_reset_cause();

#ifdef CONFIG_SOFTWARE_PANIC
	/* Restore then clear saved panic reason */
	reason = bkpdata_read(BKPDATA_INDEX_SAVED_PANIC_REASON);
	info = bkpdata_read(BKPDATA_INDEX_SAVED_PANIC_INFO);
	exception = bkpdata_read(BKPDATA_INDEX_SAVED_PANIC_EXCEPTION);
	if (reason || info || exception) {
		panic_set_reason(reason, info, exception);
		bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_REASON, 0);
		bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_INFO, 0);
		bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_EXCEPTION, 0);
	}
#endif
}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	uint32_t console_en = bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS) &
			      CONSOLE_BIT_MASK;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

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
#ifdef CONFIG_SOFTWARE_PANIC
		uint32_t reason, info;
		uint8_t exception;

		/* Panic data will be wiped by hard reset, so save it */
		panic_get_reason(&reason, &info, &exception);
		/* 16 bits stored - upper 16 bits of reason / info are lost */
		bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_REASON, reason);
		bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_INFO, info);
		bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_EXCEPTION, exception);
#endif

#ifdef CHIP_FAMILY_STM32L
		/*
		 * Ask the flash module to reboot, so that we reload the
		 * option bytes.
		 */
		flash_physical_force_reload();

		/* Fall through to watchdog if that fails */
#endif

#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
		/*
		 * On some chips, a reboot doesn't always reload the option
		 * bytes, and we need to explicitly request for a reload.
		 * The reload request triggers a chip reset, so let's just
		 * use this for hard reset.
		 */
		STM32_FLASH_CR |= STM32_FLASH_CR_OBL_LAUNCH;
#else
		/* Ask the watchdog to trigger a hard reboot */
		STM32_IWDG_KR = 0x5555;
		STM32_IWDG_RLR = 0x1;
		STM32_IWDG_KR = 0xcccc;
#endif
		/* wait for the chip to reboot */
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

int system_is_reboot_warm(void)
{
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	return ((STM32_RCC_AHBENR & 0x7e0000) == 0x7e0000);
#elif defined(CHIP_FAMILY_STM32L)
	return ((STM32_RCC_AHBENR & 0x3f) == 0x3f);
#endif
}
