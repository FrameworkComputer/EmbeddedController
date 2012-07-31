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
#include "watchdog.h"

enum bkpdata_index {
	BKPDATA_INDEX_SCRATCHPAD,	/* General-purpose scratchpad */
	BKPDATA_INDEX_WAKE,		/* Wake reasons for hibernate */
	BKPDATA_INDEX_SAVED_RESET_FLAGS,/* Saved reset flags */
	BKPDATA_INDEX_FLASH_RW_AT_BOOT, /* Flash protect RW at boot flag */
	BKPDATA_INDEX_FAKE_WP,		/* Fake write-protect pin */
					/* TODO: Remove this when we have real
					 *       write protect pin.
					 */
};

/* Wake reason flags for hibernate */
#define BKPDATA_WAKE_HIBERNATE		(1 << 0)  /* Hibernate */
#define BKPDATA_WAKE_HARD_RESET		(1 << 1)  /* Hard reset */

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
	uint32_t flags = 0;
	uint32_t raw_cause = STM32_RCC_CSR;
	uint32_t pwr_status = STM32_PWR_CSR;
	uint32_t bkp_wake_flags = bkpdata_read(BKPDATA_INDEX_WAKE);

	/* Clear the hardware reset cause by setting the RMVF bit */
	STM32_RCC_CSR |= 1 << 24;
	/* Clear SBF in PWR_CSR */
	STM32_PWR_CR |= 1 << 3;
	/* Clear hibernate wake flags */
	bkpdata_write(BKPDATA_INDEX_WAKE, 0);

	if (raw_cause & 0x60000000) {
		/* IWDG or WWDG */
		flags |= RESET_FLAG_WATCHDOG;
	}

	if (raw_cause & 0x10000000)
		flags |= RESET_FLAG_SOFT;

	if (raw_cause & 0x08000000)
		flags |= RESET_FLAG_POWER_ON;

	if (raw_cause & 0x04000000)
		flags |= RESET_FLAG_RESET_PIN;

	if (pwr_status & 0x00000002) {
		/* Hibernation and waked */
		if (bkp_wake_flags & BKPDATA_WAKE_HIBERNATE)
			flags |= RESET_FLAG_HIBERNATE;
		/* Hibernation caused by software-triggered hard reset */
		if (bkp_wake_flags & BKPDATA_WAKE_HARD_RESET)
			flags |= RESET_FLAG_HARD;
	}

	if (!flags && (raw_cause & 0xfe000000))
		flags |= RESET_FLAG_OTHER;

	/* Restore then clear saved reset flags */
	flags |= bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS);
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, 0);

	system_set_reset_flags(flags);
}


#ifdef CHIP_VARIANT_stm32f100
static inline void wait_for_RTOFF(void)
{
	while ((STM32_RTC_CRL & 0x20) == 0)
		;
}


static void __enter_hibernate_stm32f100(uint32_t seconds, uint32_t milliseconds,
					uint32_t flags)
{
	/* Store the hibernate flags */
	bkpdata_write(BKPDATA_INDEX_WAKE, flags);

	/* Enter RTC configuration mode */
	wait_for_RTOFF();
	STM32_RTC_CRL |= 0x10;
	wait_for_RTOFF();

	/* Set signal period to 0.992 ms. We want 1 ms, but 0.8% error is
	 * fine. */
	STM32_RTC_PRLL = 0x20;
	wait_for_RTOFF();

	/* Setting the alarm register */
	milliseconds = milliseconds + seconds * 1000;
	STM32_RTC_ALRH = milliseconds >> 16;
	wait_for_RTOFF();
	STM32_RTC_ALRL = milliseconds & 0xffff;
	wait_for_RTOFF();
	STM32_RTC_CNTL = 0;
	wait_for_RTOFF();
	STM32_RTC_CNTH = 0;
	wait_for_RTOFF();

	/* Enable RTC alarm interrupt */
	STM32_RTC_CRH |= 0x2;
	wait_for_RTOFF();

	/* Clear RTC alarm flag */
	STM32_RTC_CRL &= ~0x2;
	wait_for_RTOFF();

	/* Exit RTC configuration mode */
	STM32_RTC_CRL &= ~0x10;
	wait_for_RTOFF();

	/* Delay watchdog as long as possible. (~26s)
	 * TODO: Find a way to disable watchdog, perhaps through reset? */
	STM32_IWDG_KR = 0x5555;
	STM32_IWDG_PR = 0x6;
	STM32_IWDG_RLR = 0xfff;
	STM32_IWDG_KR = 0xcccc;
	watchdog_reload();

	/* Set deep sleep bit */
	CPU_SCB_SYSCTRL |= 0x4;
	/* Set power down deep sleep bit and clear wakeup flag */
	STM32_PWR_CR |= 0x6;
	/* Wait for wakeup flag cleared */
	while (STM32_PWR_CSR & 0x1)
		;
	asm volatile("wfi");
}
#endif


void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* we are going to hibernate ... */
#ifdef CHIP_VARIANT_stm32f100
	__enter_hibernate_stm32f100(seconds, (microseconds + 999) / 1000,
				    BKPDATA_WAKE_HIBERNATE);
#else
	while (1)
		/* NOT IMPLEMENTED */;
#endif
}


int system_pre_init(void)
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


void system_reset(int flags)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, save_flags);

	if (flags & SYSTEM_RESET_HARD) {
#ifdef CHIP_VARIANT_stm32f100
		/* Bounce through hibernate to trigger a hard reboot */
		__enter_hibernate_stm32f100(0, 50, BKPDATA_WAKE_HARD_RESET);
#else
		/* Hard reset not supported yet. Using soft reset. */
		CPU_NVIC_APINT = 0x05fa0004;
#endif
	} else
		CPU_NVIC_APINT = 0x05fa0004;

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
	return STRINGIFY(CHIP_VARIANT);
}


const char *system_get_chip_revision(void)
{
	return "";
}


/* TODO: crosbug.com/p/12036 */
int system_set_fake_wp(int val)
{
	return bkpdata_write(BKPDATA_INDEX_FAKE_WP, (uint16_t)val);
}


/* TODO: crosbug.com/p/12036 */
int system_get_fake_wp(void)
{
	return bkpdata_read(BKPDATA_INDEX_FAKE_WP);
}


int system_set_flash_rw_at_boot(int val)
{
	return bkpdata_write(BKPDATA_INDEX_FLASH_RW_AT_BOOT, (uint16_t)val);
}


int system_get_flash_rw_at_boot(void)
{
	return bkpdata_read(BKPDATA_INDEX_FLASH_RW_AT_BOOT);
}
