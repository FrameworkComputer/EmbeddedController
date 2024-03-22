/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "chipset.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

/* use 48Mhz USB-synchronized High-speed oscillator */
#define HSI48_CLOCK 48000000

/* use PLL at 38.4MHz as system clock. */
#define PLL_CLOCK 38400000

/* Low power idle statistics */
#ifdef CONFIG_LOW_POWER_IDLE
static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t idle_dsleep_time_us;
static int dsleep_recovery_margin_us = 1000000;

/*
 * minimum delay to enter stop mode
 *
 * STOP_MODE_LATENCY: max time to wake up from STOP mode with regulator in low
 * power mode is 5 us + PLL locking time is 200us.
 *
 * SET_RTC_MATCH_DELAY: max time to set RTC match alarm. If we set the alarm
 * in the past, it will never wake up and cause a watchdog.
 * For STM32F3, we are using HSE, which requires additional time to start up.
 * Therefore, the latency for STM32F3 is set longer.
 *
 * RESTORE_HOST_ALARM_LATENCY: max latency between the deferred routine is
 * called and the host alarm is actually restored. In practice, the max latency
 * is measured as ~600us. 1000us should be conservative enough to guarantee
 * we won't miss the host alarm.
 */
#ifdef CHIP_VARIANT_STM32F373
#define STOP_MODE_LATENCY 500 /* us */
#elif defined(CHIP_VARIANT_STM32F05X)
#define STOP_MODE_LATENCY 300 /* us */
#elif (CPU_CLOCK == PLL_CLOCK)
#define STOP_MODE_LATENCY 300 /* us */
#else
#define STOP_MODE_LATENCY 50 /* us */
#endif
#define SET_RTC_MATCH_DELAY 200 /* us */

#ifdef CONFIG_HOSTCMD_RTC
#define RESTORE_HOST_ALARM_LATENCY 1000 /* us */
#endif

#endif /* CONFIG_LOW_POWER_IDLE */

/*
 * RTC clock frequency (By default connected to LSI clock)
 *
 * The LSI on any given chip can be between 30 kHz to 60 kHz.
 * Without calibration, LSI frequency may be off by as much as 50%.
 *
 * Set synchronous clock freq to (RTC clock source / 2) to maximize
 * subsecond resolution. Set asynchronous clock to 1 Hz.
 */

#define RTC_PREDIV_A 1
#ifdef CONFIG_STM32_CLOCK_LSE
#define RTC_FREQ (32768 / (RTC_PREDIV_A + 1)) /* Hz */
/* GCD(RTC_FREQ, 1000000) */
#define RTC_GCD 64
#else /* LSI clock, 40kHz-ish */
#define RTC_FREQ (40000 / (RTC_PREDIV_A + 1)) /* Hz */
/* GCD(RTC_FREQ, 1000000) */
#define RTC_GCD 20000
#endif
#define RTC_PREDIV_S (RTC_FREQ - 1)

/*
 * There are (1000000 / RTC_FREQ) us per RTC tick, take GCD of both terms
 * for conversion calculations to fit in 32 bits.
 */
#define US_GCD (1000000 / RTC_GCD)
#define RTC_FREQ_GCD (RTC_FREQ / RTC_GCD)

uint32_t rtcss_to_us(uint32_t rtcss)
{
	return ((RTC_PREDIV_S - (rtcss & 0x7fff)) * US_GCD) / RTC_FREQ_GCD;
}

uint32_t us_to_rtcss(uint32_t us)
{
	return RTC_PREDIV_S - us * RTC_FREQ_GCD / US_GCD;
}

void config_hispeed_clock(void)
{
#ifdef CHIP_FAMILY_STM32F3
	/* Ensure that HSE is ON */
	wait_for_ready(&STM32_RCC_CR, BIT(16), BIT(17));

	/*
	 * HSE = 24MHz, no prescalar, no MCO, with PLL *2 => 48MHz SYSCLK
	 * HCLK = SYSCLK, PCLK = HCLK / 2 = 24MHz
	 * ADCCLK = PCLK / 6 = 4MHz
	 * USB uses SYSCLK = 48MHz
	 */
	STM32_RCC_CFGR = 0x0041a400;

	/* Enable the PLL */
	STM32_RCC_CR |= 0x01000000;

	/* Wait until the PLL is ready */
	while (!(STM32_RCC_CR & 0x02000000))
		;

	/* Switch SYSCLK to PLL */
	STM32_RCC_CFGR |= 0x2;

	/* Wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
/* F03X and F05X and F070 don't have HSI48 */
#elif defined(CHIP_VARIANT_STM32F03X) || defined(CHIP_VARIANT_STM32F05X) || \
	defined(CHIP_VARIANT_STM32F070)
	/* If PLL is the clock source, PLL has already been set up. */
	if ((STM32_RCC_CFGR & 0xc) == 0x8)
		return;

	/* Ensure that HSI is ON */
	wait_for_ready(&STM32_RCC_CR, BIT(0), BIT(1));

	/*
	 * HSI = 8MHz, HSI/2 with PLL *12 = ~48 MHz
	 * therefore PCLK = FCLK = SYSCLK = 48MHz
	 */
	/* Switch the PLL source to HSI/2 */
	STM32_RCC_CFGR &= ~(0x00018000);

	/*
	 * Specify HSI/2 clock as input clock to PLL and set PLL (*12).
	 */
	STM32_RCC_CFGR |= 0x00280000;

	/* Enable the PLL. */
	STM32_RCC_CR |= 0x01000000;

	/* Wait until PLL is ready. */
	while (!(STM32_RCC_CR & 0x02000000))
		;

	/* Switch SYSCLK to PLL. */
	STM32_RCC_CFGR |= 0x2;

	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
#else
	/* Ensure that HSI48 is ON */
	wait_for_ready(&STM32_RCC_CR2, BIT(16), BIT(17));

#if (CPU_CLOCK == HSI48_CLOCK)
	/*
	 * HSI48 = 48MHz, no prescaler, no MCO, no PLL
	 * therefore PCLK = FCLK = SYSCLK = 48MHz
	 * USB uses HSI48 = 48MHz
	 */

#ifdef CONFIG_USB
	/*
	 * Configure and enable Clock Recovery System
	 *
	 * Since we are running from the internal RC HSI48 clock, the CSR
	 * is needed to guarantee an accurate 48MHz clock for USB.
	 *
	 * The default values configure the CRS to use the periodic USB SOF
	 * as the SYNC signal for calibrating the HSI48.
	 *
	 */

	/* Enable Clock Recovery System */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_CRS;

	/* Enable automatic trimming */
	STM32_CRS_CR |= STM32_CRS_CR_AUTOTRIMEN;

	/* Enable oscillator clock for the frequency error counter */
	STM32_CRS_CR |= STM32_CRS_CR_CEN;
#endif

	/* switch SYSCLK to HSI48 */
	STM32_RCC_CFGR = 0x00000003;

	/* wait until the HSI48 is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0xc)
		;

#elif (CPU_CLOCK == PLL_CLOCK)
	/*
	 * HSI48 = 48MHz, no prescalar, no MCO, with PLL *4/5 => 38.4MHz SYSCLK
	 * therefore PCLK = FCLK = SYSCLK = 38.4MHz
	 * USB uses HSI48 = 48MHz
	 */

	/* If PLL is the clock source, PLL has already been set up. */
	if ((STM32_RCC_CFGR & 0xc) == 0x8)
		return;

	/*
	 * Specify HSI48 clock as input clock to PLL and set PLL multiplier
	 * and divider.
	 */
	STM32_RCC_CFGR = 0x00098000;
	STM32_RCC_CFGR2 = 0x4;

	/* Enable the PLL. */
	STM32_RCC_CR |= 0x01000000;

	/* Wait until PLL is ready. */
	while (!(STM32_RCC_CR & 0x02000000))
		;

	/* Switch SYSCLK to PLL. */
	STM32_RCC_CFGR |= 0x2;

	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;

#else
#error "CPU_CLOCK must be either 48MHz or 38.4MHz"
#endif
#endif
}

#ifdef CONFIG_HIBERNATE
void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
{
	struct rtc_time_reg rtc;

	if (seconds || microseconds)
		set_rtc_alarm(seconds, microseconds, &rtc, 0);

	/* interrupts off now */
	interrupt_disable();

#ifdef CONFIG_HIBERNATE_WAKEUP_PINS
	/* enable the wake up pins */
	STM32_PWR_CSR |= CONFIG_HIBERNATE_WAKEUP_PINS;
#endif
	STM32_PWR_CR |= 0xe;
	CPU_SCB_SYSCTRL |= 0x4;
	/* go to Standby mode */
	asm("wfi");

	/* we should never reach that point */
	while (1)
		;
}
#endif

#ifdef CONFIG_HOSTCMD_RTC
static void restore_host_wake_alarm_deferred(void)
{
	restore_host_wake_alarm();
}
DECLARE_DEFERRED(restore_host_wake_alarm_deferred);
#endif

#ifdef CONFIG_LOW_POWER_IDLE

void clock_refresh_console_in_use(void)
{
}

void __idle(void)
{
	timestamp_t t0;
	uint32_t rtc_diff;
	int next_delay, margin_us;
	struct rtc_time_reg rtc0, rtc1;

	while (1) {
		interrupt_disable();

		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

#ifdef CONFIG_LOW_POWER_IDLE_LIMITED
		if (idle_is_disabled())
			goto en_int;
#endif

		if (DEEP_SLEEP_ALLOWED &&
#ifdef CONFIG_HOSTCMD_RTC
		    /*
		     * Don't go to deep sleep mode if we might miss the
		     * wake alarm that the host requested. Note that the
		     * host alarm always aligns to second. Considering the
		     * worst case, we have to ensure alarm won't go off
		     * within RESTORE_HOST_ALARM_LATENCY + 1 second after
		     * EC exits deep sleep mode.
		     */
		    !is_host_wake_alarm_expired(
			    (timestamp_t)(next_delay + t0.val + SECOND +
					  RESTORE_HOST_ALARM_LATENCY)) &&
#endif
		    (next_delay > (STOP_MODE_LATENCY + SET_RTC_MATCH_DELAY))) {
			/* Deep-sleep in STOP mode */
			idle_dsleep_cnt++;

			uart_enable_wakeup(1);

			/* Set deep sleep bit */
			CPU_SCB_SYSCTRL |= 0x4;

			set_rtc_alarm(0, next_delay - STOP_MODE_LATENCY, &rtc0,
				      0);
			asm("wfi");

			CPU_SCB_SYSCTRL &= ~0x4;

			uart_enable_wakeup(0);

			/*
			 * By default only HSI 8MHz is enabled here. Re-enable
			 * high-speed clock if in use.
			 */
			config_hispeed_clock();

			/* Fast forward timer according to RTC counter */
			reset_rtc_alarm(&rtc1);
			rtc_diff = get_rtc_diff(&rtc0, &rtc1);
			t0.val = t0.val + rtc_diff;
			force_time(t0);

#ifdef CONFIG_HOSTCMD_RTC
			hook_call_deferred(
				&restore_host_wake_alarm_deferred_data, 0);
#endif
			/* Record time spent in deep sleep. */
			idle_dsleep_time_us += rtc_diff;

			/* Calculate how close we were to missing deadline */
			margin_us = next_delay - rtc_diff;
			if (margin_us < 0)
				/* Use CPUTS to save stack space */
				CPUTS("Idle overslept!\n");

			/* Record the closest to missing a deadline. */
			if (margin_us < dsleep_recovery_margin_us)
				dsleep_recovery_margin_us = margin_us;
		} else {
			idle_sleep_cnt++;

			/* Normal idle : only CPU clock stopped */
			asm("wfi");
		}
#ifdef CONFIG_LOW_POWER_IDLE_LIMITED
	en_int:
#endif
		interrupt_enable();
	}
}
#endif /* CONFIG_LOW_POWER_IDLE */

int clock_get_freq(void)
{
	return CPU_CLOCK;
}

void clock_wait_bus_cycles(enum bus_type bus, uint32_t cycles)
{
	volatile uint32_t unused __attribute__((unused));

	if (bus == BUS_AHB) {
		while (cycles--)
			unused = STM32_DMA1_REGS->isr;
	} else { /* APB */
		while (cycles--)
			unused = STM32_USART_BRR(STM32_USART1_BASE);
	}
}

test_mockable void clock_enable_module(enum module_id module, int enable)
{
	if (module == MODULE_ADC) {
		if (enable)
			STM32_RCC_APB2ENR |= STM32_RCC_APB2ENR_ADCEN;
		else
			STM32_RCC_APB2ENR &= ~STM32_RCC_APB2ENR_ADCEN;
		return;
	} else if (module == MODULE_USB) {
		if (enable)
			STM32_RCC_APB1ENR |= STM32_RCC_PB1_USB;
		else
			STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_USB;
	}
}

int clock_is_module_enabled(enum module_id module)
{
	if (module == MODULE_ADC)
		return !!(STM32_RCC_APB2ENR & STM32_RCC_APB2ENR_ADCEN);
	else if (module == MODULE_USB)
		return !!(STM32_RCC_APB1ENR & STM32_RCC_PB1_USB);
	return 0;
}

void rtc_init(void)
{
	rtc_unlock_regs();

	/* Enter RTC initialize mode */
	STM32_RTC_ISR |= STM32_RTC_ISR_INIT;
	while (!(STM32_RTC_ISR & STM32_RTC_ISR_INITF))
		;

	/* Set clock prescalars */
	STM32_RTC_PRER = (RTC_PREDIV_A << 16) | RTC_PREDIV_S;

	/* Start RTC timer */
	STM32_RTC_ISR &= ~STM32_RTC_ISR_INIT;
	while (STM32_RTC_ISR & STM32_RTC_ISR_INITF)
		;

	/* Enable RTC alarm interrupt */
	STM32_RTC_CR |= STM32_RTC_CR_ALRAIE | STM32_RTC_CR_BYPSHAD;
	STM32_EXTI_RTSR |= EXTI_RTC_ALR_EVENT;
	task_enable_irq(STM32_IRQ_RTC_ALARM);

	rtc_lock_regs();
}

#if defined(CONFIG_CMD_RTC) || defined(CONFIG_HOSTCMD_RTC)
void rtc_set(uint32_t sec)
{
	struct rtc_time_reg rtc;

	sec_to_rtc(sec, &rtc);
	rtc_unlock_regs();

	/* Disable alarm */
	STM32_RTC_CR &= ~STM32_RTC_CR_ALRAE;

	/* Enter RTC initialize mode */
	STM32_RTC_ISR |= STM32_RTC_ISR_INIT;
	while (!(STM32_RTC_ISR & STM32_RTC_ISR_INITF))
		;

	/* Set clock prescalars */
	STM32_RTC_PRER = (RTC_PREDIV_A << 16) | RTC_PREDIV_S;

	STM32_RTC_TR = rtc.rtc_tr;
	STM32_RTC_DR = rtc.rtc_dr;
	/* Start RTC timer */
	STM32_RTC_ISR &= ~STM32_RTC_ISR_INIT;

	rtc_lock_regs();
}
#endif

#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_COMMON_RUNTIME)
#ifdef CONFIG_CMD_IDLE_STATS
/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, const char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);
	ccprintf("Num idle calls that deep-sleep:      %d\n", idle_dsleep_cnt);
	ccprintf("Time spent in deep-sleep:            %.6llds\n",
		 idle_dsleep_time_us);
	ccprintf("Total time on:                       %.6llds\n", ts.val);
	ccprintf("Deep-sleep closest to wake deadline: %dus\n",
		 dsleep_recovery_margin_us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");
#endif /* CONFIG_CMD_IDLE_STATS */
#endif
