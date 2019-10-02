/* Copyright 2012 The Chromium OS Authors. All rights reserved.
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

#ifdef CONFIG_STM32_CLOCK_LSE
#define BDCR_SRC BDCR_SRC_LSE
#define BDCR_RDY STM32_RCC_BDCR_LSERDY
#else
#define BDCR_SRC BDCR_SRC_LSI
#define BDCR_RDY 0
#endif
#define BDCR_ENABLE_VALUE (STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC) | \
			BDCR_RDY)
#define BDCR_ENABLE_MASK (BDCR_ENABLE_VALUE | BDCR_RTCSEL_MASK | \
			STM32_RCC_BDCR_BDRST)

/* We use 16-bit BKP / BBRAM entries. */
#define STM32_BKP_ENTRIES (STM32_BKP_BYTES / 2)

/*
 * Use 32-bit for reset flags, if we have space for it:
 *  - 2 indexes are used unconditionally (SCRATCHPAD and SAVED_RESET_FLAGS)
 *  - VBNV_CONTEXT requires 8 indexes, so a total of 10 (which is the total
 *    number of entries on some STM32 variants).
 *  - Other config options are not a problem (they only take a few entries)
 *
 * Given this, we can only add an extra entry for the top 16-bit of reset flags
 * if VBNV_CONTEXT is not enabled, or if we have more than 10 entries.
 */
#if !defined(CONFIG_HOSTCMD_VBNV_CONTEXT) || STM32_BKP_ENTRIES > 10
#define CONFIG_STM32_RESET_FLAGS_EXTENDED
#endif

enum bkpdata_index {
	BKPDATA_INDEX_SCRATCHPAD,	     /* General-purpose scratchpad */
	BKPDATA_INDEX_SAVED_RESET_FLAGS,     /* Saved reset flags */
#ifdef CONFIG_STM32_RESET_FLAGS_EXTENDED
	BKPDATA_INDEX_SAVED_RESET_FLAGS_2,   /* Saved reset flags (cont) */
#endif
#ifdef CONFIG_HOSTCMD_VBNV_CONTEXT
	BKPDATA_INDEX_VBNV_CONTEXT0,
	BKPDATA_INDEX_VBNV_CONTEXT1,
	BKPDATA_INDEX_VBNV_CONTEXT2,
	BKPDATA_INDEX_VBNV_CONTEXT3,
	BKPDATA_INDEX_VBNV_CONTEXT4,
	BKPDATA_INDEX_VBNV_CONTEXT5,
	BKPDATA_INDEX_VBNV_CONTEXT6,
	BKPDATA_INDEX_VBNV_CONTEXT7,
#endif
#ifdef CONFIG_SOFTWARE_PANIC
	BKPDATA_INDEX_SAVED_PANIC_REASON,    /* Saved panic reason */
	BKPDATA_INDEX_SAVED_PANIC_INFO,      /* Saved panic data */
	BKPDATA_INDEX_SAVED_PANIC_EXCEPTION, /* Saved panic exception code */
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE
	BKPDATA_INDEX_PD0,		     /* USB-PD saved port0 state */
	BKPDATA_INDEX_PD1,		     /* USB-PD saved port1 state */
	BKPDATA_INDEX_PD2,		     /* USB-PD saved port2 state */
#endif
	BKPDATA_COUNT
};
BUILD_ASSERT(STM32_BKP_ENTRIES >= BKPDATA_COUNT);

#ifdef CONFIG_USB_PD_DUAL_ROLE
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT <= 2);
#endif

/**
 * Read backup register at specified index.
 *
 * @return The value of the register or 0 if invalid index.
 */
static uint16_t bkpdata_read(enum bkpdata_index index)
{
	if (index < 0 || index >= STM32_BKP_ENTRIES)
		return 0;

	if (index & 1)
		return STM32_BKP_DATA(index >> 1) >> 16;
	else
		return STM32_BKP_DATA(index >> 1) & 0xFFFF;
}

/**
 * Write hibernate register at specified index.
 *
 * @return nonzero if error.
 */
static int bkpdata_write(enum bkpdata_index index, uint16_t value)
{
	static struct mutex bkpdata_write_mutex;

	if (index < 0 || index >= STM32_BKP_ENTRIES)
		return EC_ERROR_INVAL;

	/*
	 * Two entries share a single 32-bit register, lock mutex to prevent
	 * read/mask/write races.
	 */
	mutex_lock(&bkpdata_write_mutex);
	if (index & 1) {
		uint32_t val = STM32_BKP_DATA(index >> 1);
		val = (val & 0x0000FFFF) | (value << 16);
		STM32_BKP_DATA(index >> 1) = val;
	} else {
		uint32_t val = STM32_BKP_DATA(index >> 1);
		val = (val & 0xFFFF0000) | value;
		STM32_BKP_DATA(index >> 1) = val;
	}
	mutex_unlock(&bkpdata_write_mutex);

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
	uint32_t raw_cause = STM32_RCC_RESET_CAUSE;
	uint32_t pwr_status = STM32_PWR_RESET_CAUSE;

#ifdef CONFIG_STM32_RESET_FLAGS_EXTENDED
	flags |= bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS_2) << 16;
#endif

	/* Clear the hardware reset cause by setting the RMVF bit */
	STM32_RCC_RESET_CAUSE |= RESET_CAUSE_RMVF;
	/* Clear SBF in PWR_CSR */
	STM32_PWR_RESET_CAUSE_CLR |= RESET_CAUSE_SBF_CLR;
	/* Clear saved reset flags */
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, 0);
#ifdef CONFIG_STM32_RESET_FLAGS_EXTENDED
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS_2, 0);
#endif

	if (raw_cause & RESET_CAUSE_WDG) {
		/*
		 * IWDG or WWDG, if the watchdog was not used as an hard reset
		 * mechanism
		 */
		if (!(flags & EC_RESET_FLAG_HARD))
			flags |= EC_RESET_FLAG_WATCHDOG;
	}

	if (raw_cause & RESET_CAUSE_SFT)
		flags |= EC_RESET_FLAG_SOFT;

	if (raw_cause & RESET_CAUSE_POR)
		flags |= EC_RESET_FLAG_POWER_ON;

	if (raw_cause & RESET_CAUSE_PIN)
		flags |= EC_RESET_FLAG_RESET_PIN;

	if (pwr_status & RESET_CAUSE_SBF)
		/* Hibernated and subsequently awakened */
		flags |= EC_RESET_FLAG_HIBERNATE;

	if (!flags && (raw_cause & RESET_CAUSE_OTHER))
		flags |= EC_RESET_FLAG_OTHER;

	/*
	 * WORKAROUND: as we cannot de-activate the watchdog during
	 * long hibernation, we are woken-up once by the watchdog and
	 * go back to hibernate if we detect that condition, without
	 * watchdog initialized this time.
	 * The RTC deadline (if any) is already set.
	 */
	if ((flags & EC_RESET_FLAG_HIBERNATE) &&
	    (flags & EC_RESET_FLAG_WATCHDOG)) {
		__enter_hibernate(0, 0);
	}

	system_set_reset_flags(flags);
}

/* Stop all timers and WDGs we might use when JTAG stops the CPU. */
void chip_pre_init(void)
{
	uint32_t apb1fz_reg = 0;
	uint32_t apb2fz_reg = 0;

#if defined(CHIP_FAMILY_STM32F0)
	apb1fz_reg =
		STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 | STM32_RCC_PB1_TIM6 |
		STM32_RCC_PB1_TIM7 | STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM15 | STM32_RCC_PB2_TIM16 |
		STM32_RCC_PB2_TIM17 | STM32_RCC_PB2_TIM1;

	/* enable clock to debug module before writing */
	STM32_RCC_APB2ENR |= STM32_RCC_DBGMCUEN;
#elif defined(CHIP_FAMILY_STM32F3)
	apb1fz_reg =
		STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 | STM32_RCC_PB1_TIM4 |
		STM32_RCC_PB1_TIM5 | STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_TIM7 |
		STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg =
		STM32_RCC_PB2_TIM15 | STM32_RCC_PB2_TIM16 | STM32_RCC_PB2_TIM17;
#elif defined(CHIP_FAMILY_STM32F4)
	/* TODO(nsanders): Implement this if someone needs jtag. */
#elif defined(CHIP_FAMILY_STM32L4)
	apb1fz_reg =
		STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 | STM32_RCC_PB1_TIM4 |
		STM32_RCC_PB1_TIM5 | STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_TIM7 |
		STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM1 | STM32_RCC_PB2_TIM8;
#elif defined(CHIP_FAMILY_STM32L)
	apb1fz_reg =
		STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 | STM32_RCC_PB1_TIM4 |
		STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM9 | STM32_RCC_PB2_TIM10 |
		STM32_RCC_PB2_TIM11;
#elif defined(CHIP_FAMILY_STM32H7)
	/* TODO(b/67081508) */
#endif

	if (apb1fz_reg)
		STM32_DBGMCU_APB1FZ |= apb1fz_reg;
	if (apb2fz_reg)
		STM32_DBGMCU_APB2FZ |= apb2fz_reg;
}

void system_pre_init(void)
{
#ifdef CONFIG_SOFTWARE_PANIC
	uint16_t reason, info;
	uint8_t exception;
#endif

	/* enable clock on Power module */
#ifndef CHIP_FAMILY_STM32H7
	STM32_RCC_APB1ENR |= STM32_RCC_PWREN;
#endif
#if defined(CHIP_FAMILY_STM32F4)
	/* enable backup registers */
	STM32_RCC_AHB1ENR |= STM32_RCC_AHB1ENR_BKPSRAMEN;
#elif defined(CHIP_FAMILY_STM32H7)
	/* enable backup registers */
	STM32_RCC_AHB4ENR |= BIT(28);
#else
	/* enable backup registers */
	STM32_RCC_APB1ENR |= BIT(27);
#endif
	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
	/* Enable access to RCC CSR register and RTC backup registers */
	STM32_PWR_CR |= BIT(8);
#ifdef CHIP_VARIANT_STM32L476
	/* Enable Vddio2 */
	STM32_PWR_CR2 |= BIT(9);
#endif

	/* switch on LSI */
	STM32_RCC_CSR |= BIT(0);
	/* Wait for LSI to be ready */
	while (!(STM32_RCC_CSR & BIT(1)))
		;
	/* re-configure RTC if needed */
#ifdef CHIP_FAMILY_STM32L
	if ((STM32_RCC_CSR & 0x00C30000) != 0x00420000) {
		/* The RTC settings are bad, we need to reset it */
		STM32_RCC_CSR |= 0x00800000;
		/* Enable RTC and use LSI as clock source */
		STM32_RCC_CSR = (STM32_RCC_CSR & ~0x00C30000) | 0x00420000;
	}
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32F4) || \
	defined(CHIP_FAMILY_STM32H7)
	if ((STM32_RCC_BDCR & BDCR_ENABLE_MASK) != BDCR_ENABLE_VALUE) {
		/* The RTC settings are bad, we need to reset it */
		STM32_RCC_BDCR |= STM32_RCC_BDCR_BDRST;
		STM32_RCC_BDCR = STM32_RCC_BDCR & ~BDCR_ENABLE_MASK;
#ifdef CONFIG_STM32_CLOCK_LSE
		/* Turn on LSE */
		STM32_RCC_BDCR |= STM32_RCC_BDCR_LSEON;
		/* Wait for LSE to be ready */
		while (!(STM32_RCC_BDCR & STM32_RCC_BDCR_LSERDY))
			;
#endif
		/* Select clock source and enable RTC */
		STM32_RCC_BDCR |= BDCR_RTCSEL(BDCR_SRC) | STM32_RCC_BDCR_RTCEN;
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

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | EC_RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= EC_RESET_FLAG_AP_OFF;

	/* Remember that the software asked us to hard reboot */
	if (flags & SYSTEM_RESET_HARD)
		save_flags |= EC_RESET_FLAG_HARD;

#ifdef CONFIG_STM32_RESET_FLAGS_EXTENDED
	if (flags & SYSTEM_RESET_AP_WATCHDOG)
		save_flags |= EC_RESET_FLAG_AP_WATCHDOG;

	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, save_flags & 0xffff);
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS_2, save_flags >> 16);
#else
	/* Reset flags are 32-bits, but BBRAM entry is only 16 bits. */
	ASSERT(!(save_flags >> 16));
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, save_flags);
#endif

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
		STM32_FLASH_CR |= FLASH_CR_OBL_LAUNCH;
#elif defined(CHIP_FAMILY_STM32L4)
		STM32_FLASH_KEYR = FLASH_KEYR_KEY1;
		STM32_FLASH_KEYR = FLASH_KEYR_KEY2;
		STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY1;
		STM32_FLASH_OPTKEYR = FLASH_OPTKEYR_KEY2;
		STM32_FLASH_CR |= FLASH_CR_OBL_LAUNCH;
#else
		/*
		 * RM0433 Rev 6
		 * Section 44.3.3
		 * https://www.st.com/resource/en/reference_manual/dm00314099.pdf#page=1898
		 *
		 * When the window option is not used, the IWDG can be
		 * configured as follows:
		 *
		 * 1. Enable the IWDG by writing 0x0000 CCCC in the Key
		 *    register (IWDG_KR).
		 * 2. Enable register access by writing 0x0000 5555 in the Key
		 *    register (IWDG_KR).
		 * 3. Write the prescaler by programming the Prescaler register
		 *    (IWDG_PR) from 0 to 7.
		 * 4. Write the Reload register (IWDG_RLR).
		 * 5. Wait for the registers to be updated
		 *    (IWDG_SR = 0x0000 0000).
		 * 6. Refresh the counter value with IWDG_RLR
		 *    (IWDG_KR = 0x0000 AAAA)
		 */

		/*
		 * Enable IWDG, which shouldn't be necessary since the IWDG
		 * only needs to be started once, but STM32F412 hangs unless
		 * this is added.
		 *
		 * See http://b/137045370.
		 */
		STM32_IWDG_KR = STM32_IWDG_KR_START;

		/* Ask the watchdog to trigger a hard reboot */
		STM32_IWDG_KR = STM32_IWDG_KR_UNLOCK;
		STM32_IWDG_RLR = 0x1;
		/* Wait for value to be reloaded. */
		while (STM32_IWDG_SR & STM32_IWDG_SR_RVU)
			;
		STM32_IWDG_KR = STM32_IWDG_KR_RELOAD;
#endif
		/* wait for the chip to reboot */
		while (1)
			;
	} else {
		if (flags & SYSTEM_RESET_WAIT_EXT) {
			int i;

			/* Wait 10 seconds for external reset */
			for (i = 0; i < 1000; i++) {
				watchdog_reload();
				udelay(10000);
			}
		}
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
	return STRINGIFY(CHIP_VARIANT);
}

const char *system_get_chip_revision(void)
{
	return "";
}

int system_get_chip_unique_id(uint8_t **id)
{
	*id = (uint8_t *)STM32_UNIQUE_ID_ADDRESS;
	return STM32_UNIQUE_ID_LENGTH;
}

static int bkpdata_index_lookup(enum system_bbram_idx idx, int *msb)
{
	*msb = 0;

#ifdef CONFIG_HOSTCMD_VBNV_CONTEXT
	if (idx >= SYSTEM_BBRAM_IDX_VBNVBLOCK0 &&
	    idx <= SYSTEM_BBRAM_IDX_VBNVBLOCK15) {
		*msb = (idx - SYSTEM_BBRAM_IDX_VBNVBLOCK0) % 2;
		return BKPDATA_INDEX_VBNV_CONTEXT0 +
		       (idx - SYSTEM_BBRAM_IDX_VBNVBLOCK0) / 2;
	}
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE
	if (idx == SYSTEM_BBRAM_IDX_PD0)
		return BKPDATA_INDEX_PD0;
	if (idx == SYSTEM_BBRAM_IDX_PD1)
		return BKPDATA_INDEX_PD1;
	if (idx == SYSTEM_BBRAM_IDX_PD2)
		return BKPDATA_INDEX_PD2;
#endif
	return -1;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int msb = 0;
	int bkpdata_index = bkpdata_index_lookup(idx, &msb);

	if (bkpdata_index < 0)
		return EC_ERROR_INVAL;

	*value = (bkpdata_read(bkpdata_index) >> (8 * msb)) & 0xff;
	return EC_SUCCESS;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	uint16_t read;
	int msb = 0;
	int bkpdata_index = bkpdata_index_lookup(idx, &msb);

	if (bkpdata_index < 0)
		return EC_ERROR_INVAL;

	read = bkpdata_read(bkpdata_index);
	if (msb)
		read = (read & 0xff) | (value << 8);
	else
		read = (read & 0xff00) | value;

	bkpdata_write(bkpdata_index, read);
	return EC_SUCCESS;
}

int system_is_reboot_warm(void)
{
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	return ((STM32_RCC_AHBENR & 0x7e0000) == 0x7e0000);
#elif defined(CHIP_FAMILY_STM32L)
	return ((STM32_RCC_AHBENR & 0x3f) == 0x3f);
#elif defined(CHIP_FAMILY_STM32L4)
	return ((STM32_RCC_AHB2ENR & STM32_RCC_AHB2ENR_GPIOMASK)
			== STM32_RCC_AHB2ENR_GPIOMASK);
#elif defined(CHIP_FAMILY_STM32F4)
	return ((STM32_RCC_AHB1ENR & STM32_RCC_AHB1ENR_GPIOMASK)
			== STM32_RCC_AHB1ENR_GPIOMASK);
#elif defined(CHIP_FAMILY_STM32H7)
	return ((STM32_RCC_AHB4ENR & STM32_RCC_AHB4ENR_GPIOMASK)
			== STM32_RCC_AHB4ENR_GPIOMASK);
#endif
}
