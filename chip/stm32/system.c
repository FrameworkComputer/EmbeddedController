/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "bkpdata.h"
#include "clock.h"
#include "console.h"
#include "cpu.h"
#include "cros_version.h"
#include "flash.h"
#include "gpio_chip.h"
#include "hooks.h"
#include "host_command.h"
#include "panic.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"

#ifdef CONFIG_STM32_CLOCK_LSE
#define BDCR_SRC BDCR_SRC_LSE
#define BDCR_RDY STM32_RCC_BDCR_LSERDY
#else
#define BDCR_SRC BDCR_SRC_LSI
#define BDCR_RDY 0
#endif
#define BDCR_ENABLE_VALUE \
	(STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC) | BDCR_RDY)
#define BDCR_ENABLE_MASK \
	(BDCR_ENABLE_VALUE | BDCR_RTCSEL_MASK | STM32_RCC_BDCR_BDRST)

#ifdef CONFIG_USB_PD_DUAL_ROLE
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT <= 3);
#endif

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
	crec_msleep(100);
#endif

	/* Flush console before hibernating */
	cflush();

	if (board_hibernate)
		board_hibernate();

	/* chip specific standby mode */
	__enter_hibernate(seconds, microseconds);
}

uint32_t chip_read_reset_flags(void)
{
	return bkpdata_read_reset_flags();
}

void chip_save_reset_flags(uint32_t flags)
{
	bkpdata_write_reset_flags(flags);
}

static void check_reset_cause(void)
{
	uint32_t flags = chip_read_reset_flags();
	uint32_t raw_cause = STM32_RCC_RESET_CAUSE;
#ifdef STM32_PWR_RESET_CAUSE
	uint32_t pwr_status = STM32_PWR_RESET_CAUSE;
#endif

	/* Clear the hardware reset cause by setting the RMVF bit */
	STM32_RCC_RESET_CAUSE |= RESET_CAUSE_RMVF;
#ifdef STM32_PWR_RESET_CAUSE
	/* Clear SBF in PWR_CSR */
	STM32_PWR_RESET_CAUSE_CLR |= RESET_CAUSE_SBF_CLR;
#endif
	/* Clear saved reset flags */
	chip_save_reset_flags(0);

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

#ifdef STM32_PWR_RESET_CAUSE
	if (pwr_status & RESET_CAUSE_SBF)
		/* Hibernated and subsequently awakened */
		flags |= EC_RESET_FLAG_HIBERNATE;
#endif

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
	apb1fz_reg = STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 |
		     STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_TIM7 |
		     STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM15 | STM32_RCC_PB2_TIM16 |
		     STM32_RCC_PB2_TIM17 | STM32_RCC_PB2_TIM1;

	/* enable clock to debug module before writing */
	STM32_RCC_APB2ENR |= STM32_RCC_DBGMCUEN;
#elif defined(CHIP_FAMILY_STM32F3)
	apb1fz_reg = STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 |
		     STM32_RCC_PB1_TIM4 | STM32_RCC_PB1_TIM5 |
		     STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_TIM7 |
		     STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM15 | STM32_RCC_PB2_TIM16 |
		     STM32_RCC_PB2_TIM17;
#elif defined(CHIP_FAMILY_STM32F4)
	apb1fz_reg = STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 |
		     STM32_RCC_PB1_TIM4 | STM32_RCC_PB1_TIM5 |
		     STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_TIM7 |
		     STM32_RCC_PB1_TIM12 | STM32_RCC_PB1_TIM13 |
		     STM32_RCC_PB1_TIM14 | STM32_RCC_PB1_RTC |
		     STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM1 | STM32_RCC_PB2_TIM8 |
		     STM32_RCC_PB2_TIM9 | STM32_RCC_PB2_TIM10 |
		     STM32_RCC_PB2_TIM11;
#elif defined(CHIP_FAMILY_STM32L4)

#ifdef CHIP_VARIANT_STM32L431X
	apb1fz_reg = STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM7 |
		     STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_WWDG |
		     STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM1 | STM32_RCC_PB2_TIM15 |
		     STM32_RCC_PB2_TIM16;
#else
	apb1fz_reg = STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 |
		     STM32_RCC_PB1_TIM4 | STM32_RCC_PB1_TIM5 |
		     STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_TIM7 |
		     STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM1 | STM32_RCC_PB2_TIM8;
#endif
#elif defined(CHIP_FAMILY_STM32L5)
	apb1fz_reg = STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 |
		     STM32_RCC_PB1_TIM4 | STM32_RCC_PB1_TIM5 |
		     STM32_RCC_PB1_TIM6 | STM32_RCC_PB1_TIM7 |
		     STM32_RCC_PB1_WWDG | STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM1 | STM32_RCC_PB2_TIM8 |
		     STM32_RCC_PB2_TIM15 | STM32_RCC_PB2_TIM16 |
		     STM32_RCC_PB2_TIM17;
#elif defined(CHIP_FAMILY_STM32L)
	apb1fz_reg = STM32_RCC_PB1_TIM2 | STM32_RCC_PB1_TIM3 |
		     STM32_RCC_PB1_TIM4 | STM32_RCC_PB1_WWDG |
		     STM32_RCC_PB1_IWDG;
	apb2fz_reg = STM32_RCC_PB2_TIM9 | STM32_RCC_PB2_TIM10 |
		     STM32_RCC_PB2_TIM11;
#elif defined(CHIP_FAMILY_STM32G4)
	apb1fz_reg = STM32_DBGMCU_APB1FZ_TIM2 | STM32_DBGMCU_APB1FZ_TIM3 |
		     STM32_DBGMCU_APB1FZ_TIM4 | STM32_DBGMCU_APB1FZ_TIM5 |
		     STM32_DBGMCU_APB1FZ_TIM6 | STM32_DBGMCU_APB1FZ_TIM7 |
		     STM32_DBGMCU_APB1FZ_RTC | STM32_DBGMCU_APB1FZ_WWDG |
		     STM32_DBGMCU_APB1FZ_IWDG;
	apb2fz_reg = STM32_DBGMCU_APB2FZ_TIM1 | STM32_DBGMCU_APB2FZ_TIM8 |
		     STM32_DBGMCU_APB2FZ_TIM15 | STM32_DBGMCU_APB2FZ_TIM16 |
		     STM32_DBGMCU_APB2FZ_TIM17 | STM32_DBGMCU_APB2FZ_TIM20;
#elif defined(CHIP_FAMILY_STM32H7)
	/* TODO(b/67081508) */
#endif
	if (apb1fz_reg)
		STM32_DBGMCU_APB1FZ |= apb1fz_reg;
	if (apb2fz_reg)
		STM32_DBGMCU_APB2FZ |= apb2fz_reg;
}

#ifdef CONFIG_PVD
/******************************************************************************
 * Detects sagging Vdd voltage and resets the system via the programmable
 * voltage detector interrupt.
 */
static void configure_pvd(void)
{
	/* Clear Interrupt Enable Mask Register. */
	STM32_EXTI_IMR &= ~EXTI_PVD_EVENT;

	/* Clear Rising and Falling Trigger Selection Registers. */
	STM32_EXTI_RTSR &= ~EXTI_PVD_EVENT;
	STM32_EXTI_FTSR &= ~EXTI_PVD_EVENT;

	/* Clear the value of the PVD Level Selection. */
	STM32_PWR_CR &= ~STM32_PWD_PVD_LS_MASK;

	/* Set the new value of the PVD Level Selection. */
	STM32_PWR_CR |= STM32_PWD_PVD_LS(PVD_THRESHOLD);

	/* Enable Power Clock. */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_PWREN;

	/* Configure the NVIC for PVD. */
	task_enable_irq(STM32_IRQ_PVD);

	/* Configure interrupt mode. */
	STM32_EXTI_IMR |= EXTI_PVD_EVENT;
	STM32_EXTI_RTSR |= EXTI_PVD_EVENT;

	/* Enable the PVD Output. */
	STM32_PWR_CR |= STM32_PWR_PVDE;
}

static void pvd_interrupt(void)
{
	/* Clear Pending Register */
	STM32_EXTI_PR = EXTI_PVD_EVENT;
	/* Handle recovery by rebooting the system */
	system_reset(0);
}
DECLARE_IRQ(STM32_IRQ_PVD, pvd_interrupt, HOOK_PRIO_FIRST);

#endif /* CONFIG_PVD */

void system_pre_init(void)
{
	uint16_t reason, info;
	uint8_t exception, panic_flags;
	struct panic_data *pdata;

	/* enable clock on Power module */
#ifndef CHIP_FAMILY_STM32H7
#if defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32L5)
	STM32_RCC_APB1ENR1 |= STM32_RCC_PWREN;
#else
	STM32_RCC_APB1ENR |= STM32_RCC_PWREN;
#endif
#endif
#if defined(CHIP_FAMILY_STM32F4)
	/* enable backup registers */
	STM32_RCC_AHB1ENR |= STM32_RCC_AHB1ENR_BKPSRAMEN;
#elif defined(CHIP_FAMILY_STM32H7)
	/* enable backup registers */
	STM32_RCC_AHB4ENR |= BIT(28);
#elif defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32L5)
	/* enable RTC APB clock */
	STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_RTCAPBEN;
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

#if defined(CHIP_FAMILY_STM32G4)
	/* Make sure PWR clock is enabled */
	STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_PWREN;
	/* Enable access to backup domain registers */
	STM32_PWR_CR1 |= STM32_PWR_CR1_DBP;
#endif
	/* re-configure RTC if needed */
#ifdef CHIP_FAMILY_STM32L
	if ((STM32_RCC_CSR & 0x00C30000) != 0x00420000) {
		/* The RTC settings are bad, we need to reset it */
		STM32_RCC_CSR |= 0x00800000;
		/* Enable RTC and use LSI as clock source */
		STM32_RCC_CSR = (STM32_RCC_CSR & ~0x00C30000) | 0x00420000;
	}
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) ||   \
	defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32L5) || \
	defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32H7) || \
	defined(CHIP_FAMILY_STM32G4)
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

	/*
	 * Older ROs restore reason, info, and exception, but do not support
	 * the saved panic flags. In that case, we will let RW handle restoring
	 * the panic flags. If we get to this point in the code and the panic
	 * data does not exist, it doesn't make sense to try to only restore
	 * the panic flags, the information was lost.
	 */
	pdata = panic_get_data();
	panic_flags = bkpdata_read(BKPDATA_INDEX_SAVED_PANIC_FLAGS);
	if (pdata && panic_flags) {
		pdata->flags = panic_flags;
		bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_FLAGS, 0);
	}

#ifdef CONFIG_PVD
	configure_pvd();
#endif
}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/*
	 * TODO(crbug.com/1045283): Change this part of code to use
	 * system_encode_save_flags, like all other system_reset functions.
	 *
	 * system_encode_save_flags(flags, &save_flags);
	 */

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | EC_RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= EC_RESET_FLAG_AP_OFF;

	/* Remember that the software asked us to hard reboot */
	if (flags & SYSTEM_RESET_HARD)
		save_flags |= EC_RESET_FLAG_HARD;

	/* Add in stay in RO flag into saved flags. */
	if (flags & SYSTEM_RESET_STAY_IN_RO)
		save_flags |= EC_RESET_FLAG_STAY_IN_RO;

	if (flags & SYSTEM_RESET_AP_WATCHDOG)
		save_flags |= EC_RESET_FLAG_AP_WATCHDOG;

	chip_save_reset_flags(save_flags);

#ifdef CONFIG_ARMV7M_CACHE
	/*
	 * Disable caches (D-cache is also flushed and invalidated)
	 * so changes that lives in cache are saved in memory now.
	 * Any subsequent writes will be done immediately.
	 */
	cpu_disable_caches();
#endif

	if (flags & SYSTEM_RESET_HARD) {
		/* Panic data will be wiped by hard reset, so save it */
		uint32_t reason, info;
		uint8_t exception, panic_flags;
		struct panic_data *pdata = panic_get_data();

		if (pdata) {
			panic_flags = pdata->flags;
			panic_get_reason(&reason, &info, &exception);
			/*
			 * 16 bits stored - upper 16 bits of reason / info
			 * are lost.
			 */
			bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_REASON, reason);
			bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_INFO, info);
			bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_EXCEPTION,
				      exception);
			bkpdata_write(BKPDATA_INDEX_SAVED_PANIC_FLAGS,
				      panic_flags);
		}

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32L4)
		/*
		 * Ask the flash module to reboot, so that we reload the
		 * option bytes.
		 */
		crec_flash_physical_force_reload();

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
#elif defined(CHIP_FAMILY_STM32G4)
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
		 * RM0433 Rev 7
		 * Section 45.4.4 Page 1920
		 * https://www.st.com/resource/en/reference_manual/dm00314099.pdf
		 * If several reload, prescaler, or window values are used by
		 * the application, it is mandatory to wait until RVU bit is
		 * reset before changing the reload value, to wait until PVU bit
		 * is reset before changing the prescaler value, and to wait
		 * until WVU bit is reset before changing the window value.
		 *
		 * Here we should wait to finish previous IWDG_RLR register
		 * update (see watchdog_init()) before starting next update,
		 * otherwise new IWDG_RLR value will be lost.
		 */
		while (STM32_IWDG_SR & STM32_IWDG_SR_RVU)
			;

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
		/* Wait for value to be updated. */
		while (STM32_IWDG_SR & STM32_IWDG_SR_RVU)
			;

		/* Reload IWDG counter, it also locks registers */
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

		/* Request a soft system reset from the core. */
		CPU_NVIC_APINT = CPU_NVIC_APINT_KEY_WR | CPU_NVIC_APINT_SYSRST;
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

int system_get_scratchpad(uint32_t *value)
{
	*value = (uint32_t)bkpdata_read(BKPDATA_INDEX_SCRATCHPAD);
	return EC_SUCCESS;
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
	/*
	 * Detecting if the system is warm is relevant for a
	 * few reasons.
	 * One such reason is that some firmwares transition from
	 * RO to RW images. When this happens, we may not need to
	 * restart certain clocks. On the flip side, we may need
	 * to restart the clocks if the RW requires a different
	 * set of clocks. Thus, the clock configurations need to
	 * be checked for a perfect match.
	 */

#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
	return ((STM32_RCC_AHBENR & 0x7e0000) == 0x7e0000);
#elif defined(CHIP_FAMILY_STM32L)
	return ((STM32_RCC_AHBENR & 0x3f) == 0x3f);
#elif defined(CHIP_FAMILY_STM32L4)
	return ((STM32_RCC_AHB2ENR & STM32_RCC_AHB2ENR_GPIOMASK) ==
		STM32_RCC_AHB2ENR_GPIOMASK);
#elif defined(CHIP_FAMILY_STM32L5)
	return ((STM32_RCC_AHB2ENR & STM32_RCC_AHB2ENR_GPIOMASK) ==
		STM32_RCC_AHB2ENR_GPIOMASK);
#elif defined(CHIP_FAMILY_STM32F4)
	return ((STM32_RCC_AHB1ENR & STM32_RCC_AHB1ENR_GPIOMASK) ==
		gpio_required_clocks());
#elif defined(CHIP_FAMILY_STM32G4)
	return ((STM32_RCC_AHB2ENR & STM32_RCC_AHB2ENR_GPIOMASK) ==
		gpio_required_clocks());
#elif defined(CHIP_FAMILY_STM32H7)
	return ((STM32_RCC_AHB4ENR & STM32_RCC_AHB4ENR_GPIOMASK) ==
		STM32_RCC_AHB4ENR_GPIOMASK);
#endif
}
