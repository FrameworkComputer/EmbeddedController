/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "builtin/assert.h"
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
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

enum clock_osc {
	OSC_HSI = 0, /* High-speed internal oscillator */
	OSC_HSE, /* High-speed external oscillator */
	OSC_PLL, /* PLL */
};

/*
 * NOTE: Sweetberry requires MCO2 <- HSE @ 24MHz
 * MCO outputs are selected here but are not changeable later.
 * A CONFIG may be needed if other boards have different MCO
 * requirements.
 */
#define RCC_CFGR_MCO_CONFIG             \
	((2 << 30) | /* MCO2 <- HSE  */ \
	 (0 << 27) | /* MCO2 div / 4 */ \
	 (6 << 24) | /* MCO1 div / 4 */ \
	 (3 << 21)) /* MCO1 <- PLL  */

#ifdef CONFIG_STM32_CLOCK_HSE_HZ
/* RTC clock must 1 Mhz when derived from HSE */
#define RTC_DIV DIV_ROUND_NEAREST(CONFIG_STM32_CLOCK_HSE_HZ, STM32F4_RTC_REQ)
#else /* !CONFIG_STM32_CLOCK_HSE_HZ */
/* RTC clock not derived from HSE, turn it off */
#define RTC_DIV 0
#endif /* CONFIG_STM32_CLOCK_HSE_HZ */

/* Bus clocks dividers depending on the configuration */
/*
 * max speed configuration with the PLL ON
 * as defined in the registers file.
 * For STM32F446: max 45 MHz
 * For STM32F412: max AHB 100 MHz / APB2 100 Mhz / APB1 50 Mhz
 */
#define RCC_CFGR_DIVIDERS_WITH_PLL                                     \
	(RCC_CFGR_MCO_CONFIG | CFGR_RTCPRE(RTC_DIV) |                  \
	 CFGR_PPRE2(STM32F4_APB2_PRE) | CFGR_PPRE1(STM32F4_APB1_PRE) | \
	 CFGR_HPRE(STM32F4_AHB_PRE))
/*
 * lower power configuration without the PLL
 * the frequency will be low (8-24Mhz), we don't want dividers to the
 * peripheral clocks, put /1 everywhere.
 */
#define RCC_CFGR_DIVIDERS_NO_PLL                                \
	(RCC_CFGR_MCO_CONFIG | CFGR_RTCPRE(0) | CFGR_PPRE2(0) | \
	 CFGR_PPRE1(0) | CFGR_HPRE(0))

/* PLL output frequency */
#define STM32F4_PLL_CLOCK (STM32F4_VCO_CLOCK / STM32F4_PLLP_DIV)

/* current clock settings (PLL is initialized at startup) */
static int current_osc = OSC_PLL;
static int current_io_freq = STM32F4_IO_CLOCK;
static int current_timer_freq = STM32F4_TIMER_CLOCK;

/* the EC code expects to get the USART/I2C clock frequency here (APB clock) */
int clock_get_freq(void)
{
	return current_io_freq;
}

int clock_get_timer_freq(void)
{
	return current_timer_freq;
}

static void clock_enable_osc(enum clock_osc osc, bool enabled)
{
	uint32_t ready;
	uint32_t on;

	switch (osc) {
	case OSC_HSI:
		ready = STM32_RCC_CR_HSIRDY;
		on = STM32_RCC_CR_HSION;
		break;
	case OSC_HSE:
		ready = STM32_RCC_CR_HSERDY;
		on = STM32_RCC_CR_HSEON;
		break;
	case OSC_PLL:
		ready = STM32_RCC_CR_PLLRDY;
		on = STM32_RCC_CR_PLLON;
		break;
	default:
		ASSERT(0);
		return;
	}

	/* Turn off the oscillator, but don't wait for shutdown */
	if (!enabled) {
		STM32_RCC_CR &= ~on;
		return;
	}

	/* Turn on the oscillator if not already on */
	wait_for_ready(&STM32_RCC_CR, on, ready);
}

static void clock_switch_osc(enum clock_osc osc)
{
	uint32_t sw;
	uint32_t sws;

	switch (osc) {
	case OSC_HSI:
		sw = STM32_RCC_CFGR_SW_HSI | RCC_CFGR_DIVIDERS_NO_PLL;
		sws = STM32_RCC_CFGR_SWS_HSI;
		break;
	case OSC_HSE:
		sw = STM32_RCC_CFGR_SW_HSE | RCC_CFGR_DIVIDERS_NO_PLL;
		sws = STM32_RCC_CFGR_SWS_HSE;
		break;
	case OSC_PLL:
		sw = STM32_RCC_CFGR_SW_PLL | RCC_CFGR_DIVIDERS_WITH_PLL;
		sws = STM32_RCC_CFGR_SWS_PLL;
		break;
	default:
		return;
	}

	STM32_RCC_CFGR = sw;
	while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) != sws)
		;
}

void clock_set_osc(enum clock_osc osc)
{
	volatile uint32_t unused __attribute__((unused));

	if (osc == current_osc)
		return;

	hook_notify(HOOK_PRE_FREQ_CHANGE);

	switch (osc) {
	default:
	case OSC_HSI:
		/* new clock settings: no dividers */
		current_io_freq = STM32F4_HSI_CLOCK;
		current_timer_freq = STM32F4_HSI_CLOCK;
		/* Switch to HSI */
		clock_switch_osc(OSC_HSI);
		/* optimized flash latency settings for <30Mhz clock (0-WS) */
		STM32_FLASH_ACR =
			(STM32_FLASH_ACR & ~STM32_FLASH_ACR_LAT_MASK) |
			STM32_FLASH_ACR_LATENCY_SLOW;
		/* read-back the latency as advised by the Reference Manual */
		unused = STM32_FLASH_ACR;
		/* Turn off the PLL1 to save power */
		clock_enable_osc(OSC_PLL, false);
		break;

#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	case OSC_HSE:
		/* new clock settings: no dividers */
		current_io_freq = CONFIG_STM32_CLOCK_HSE_HZ;
		current_timer_freq = CONFIG_STM32_CLOCK_HSE_HZ;
		/* Switch to HSE */
		clock_switch_osc(OSC_HSE);
		/* optimized flash latency settings for <30Mhz clock (0-WS) */
		STM32_FLASH_ACR =
			(STM32_FLASH_ACR & ~STM32_FLASH_ACR_LAT_MASK) |
			STM32_FLASH_ACR_LATENCY_SLOW;
		/* read-back the latency as advised by the Reference Manual */
		unused = STM32_FLASH_ACR;
		/* Turn off the PLL1 to save power */
		clock_enable_osc(OSC_PLL, false);
		break;
#endif /* CONFIG_STM32_CLOCK_HSE_HZ */

	case OSC_PLL:
		/* new clock settings */
		current_io_freq = STM32F4_IO_CLOCK;
		current_timer_freq = STM32F4_TIMER_CLOCK;
		/* turn on PLL and wait until it's ready */
		clock_enable_osc(OSC_PLL, true);
		/*
		 * Increase flash latency before transition the clock
		 * Use the minimum Wait States value optimized for the platform.
		 */
		STM32_FLASH_ACR =
			(STM32_FLASH_ACR & ~STM32_FLASH_ACR_LAT_MASK) |
			STM32_FLASH_ACR_LATENCY;
		/* read-back the latency as advised by the Reference Manual */
		unused = STM32_FLASH_ACR;
		/* Switch to PLL */
		clock_switch_osc(OSC_PLL);

		break;
	}

	current_osc = osc;
	hook_notify(HOOK_FREQ_CHANGE);
}

static void clock_pll_configure(void)
{
#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	int srcclock = CONFIG_STM32_CLOCK_HSE_HZ;
#else
	int srcclock = STM32F4_HSI_CLOCK;
#endif
	int plldiv, pllinputclock;
	int pllmult, vcoclock;
	int systemclock;
	int usbdiv;
	int i2sdiv;

	/* PLL input must be between 1-2MHz, near 2 */
	/* Valid values 2-63 */
	plldiv = (srcclock + STM32F4_PLL_REQ - 1) / STM32F4_PLL_REQ;
	pllinputclock = srcclock / plldiv;

	/* PLL output clock: Must be 100-432MHz */
	pllmult = (STM32F4_VCO_CLOCK + (pllinputclock / 2)) / pllinputclock;
	vcoclock = pllinputclock * pllmult;

	/* CPU/System clock */
	systemclock = vcoclock / STM32F4_PLLP_DIV;
	/* USB clock = 48MHz exactly */
	usbdiv = (vcoclock + (STM32F4_USB_REQ / 2)) / STM32F4_USB_REQ;
	assert(vcoclock / usbdiv == STM32F4_USB_REQ);

	/* SYSTEM/I2S: same system clock */
	i2sdiv = (vcoclock + (systemclock / 2)) / systemclock;

	/* Set up PLL */
	STM32_RCC_PLLCFGR = PLLCFGR_PLLM(plldiv) | PLLCFGR_PLLN(pllmult) |
			    PLLCFGR_PLLP(STM32F4_PLLP_DIV / 2 - 1) |
#if defined(CONFIG_STM32_CLOCK_HSE_HZ)
			    PLLCFGR_PLLSRC_HSE |
#else
			    PLLCFGR_PLLSRC_HSI |
#endif
			    PLLCFGR_PLLQ(usbdiv) | PLLCFGR_PLLR(i2sdiv);
}

void low_power_init(void);

void config_hispeed_clock(void)
{
#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	/* Ensure that HSE is ON */
	clock_enable_osc(OSC_HSE, true);
#endif

	/* Put the PLL settings, they are never changing */
	clock_pll_configure();
	clock_enable_osc(OSC_PLL, true);

	/* Switch SYSCLK to PLL, setup bus prescalers. */
	clock_switch_osc(OSC_PLL);

#ifdef CONFIG_LOW_POWER_IDLE
	low_power_init();
#endif
}

void clock_wait_bus_cycles(enum bus_type bus, uint32_t cycles)
{
	volatile uint32_t unused __attribute__((unused));

	if (bus == BUS_AHB) {
		while (cycles--)
			unused = STM32_DMA_GET_ISR(0);
	} else { /* APB */
		while (cycles--)
			unused = STM32_USART_BRR(STM32_USART1_BASE);
	}
}

test_mockable void clock_enable_module(enum module_id module, int enable)
{
	if (module == MODULE_USB) {
		if (enable) {
			STM32_RCC_AHB2ENR |= STM32_RCC_AHB2ENR_OTGFSEN;
			STM32_RCC_AHB1ENR |= STM32_RCC_AHB1ENR_OTGHSEN |
					     STM32_RCC_AHB1ENR_OTGHSULPIEN;
		} else {
			STM32_RCC_AHB2ENR &= ~STM32_RCC_AHB2ENR_OTGFSEN;
			STM32_RCC_AHB1ENR &= ~STM32_RCC_AHB1ENR_OTGHSEN &
					     ~STM32_RCC_AHB1ENR_OTGHSULPIEN;
		}
		return;
	} else if (module == MODULE_I2C) {
		if (enable) {
			/* Enable clocks to I2C modules if necessary */
			STM32_RCC_APB1ENR |=
				STM32_RCC_I2C1EN | STM32_RCC_I2C2EN |
				STM32_RCC_I2C3EN | STM32_RCC_FMPI2C4EN;
			STM32_RCC_DCKCFGR2 =
				(STM32_RCC_DCKCFGR2 &
				 ~DCKCFGR2_FMPI2C1SEL_MASK) |
				DCKCFGR2_FMPI2C1SEL(FMPI2C1SEL_APB);
		} else {
			STM32_RCC_APB1ENR &=
				~(STM32_RCC_I2C1EN | STM32_RCC_I2C2EN |
				  STM32_RCC_I2C3EN | STM32_RCC_FMPI2C4EN);
		}
		return;
	} else if (module == MODULE_ADC) {
		if (enable)
			STM32_RCC_APB2ENR |= STM32_RCC_APB2ENR_ADC1EN;
		else
			STM32_RCC_APB2ENR &= ~STM32_RCC_APB2ENR_ADC1EN;
		return;
	}
}

/* Real Time Clock (RTC) */

#ifdef CONFIG_STM32_CLOCK_HSE_HZ
#define RTC_PREDIV_A 39
#define RTC_FREQ ((STM32F4_RTC_REQ) / (RTC_PREDIV_A + 1)) /* Hz */
#else /* from LSI clock */
#define RTC_PREDIV_A 1
#define RTC_FREQ (STM32F4_LSI_CLOCK / (RTC_PREDIV_A + 1)) /* Hz */
#endif
#define RTC_PREDIV_S (RTC_FREQ - 1)
/*
 * Scaling factor to ensure that the intermediate values computed from/to the
 * RTC frequency are fitting in a 32-bit integer.
 */
#define SCALING 1000

uint32_t rtcss_to_us(uint32_t rtcss)
{
	return ((RTC_PREDIV_S - rtcss) * (SECOND / SCALING) /
		(RTC_FREQ / SCALING));
}

uint32_t us_to_rtcss(uint32_t us)
{
	return (RTC_PREDIV_S -
		(us * (RTC_FREQ / SCALING) / (SECOND / SCALING)));
}

void rtc_init(void)
{
	/* Setup RTC Clock input */
#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	/* RTC clocked from the HSE */
	STM32_RCC_BDCR = STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC_HSE);
#else
	/* RTC clocked from the LSI, ensure first it is ON */
	wait_for_ready(&(STM32_RCC_CSR), STM32_RCC_CSR_LSION,
		       STM32_RCC_CSR_LSIRDY);

	STM32_RCC_BDCR = STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC_LSI);
#endif

	rtc_unlock_regs();

	/* Enter RTC initialize mode */
	STM32_RTC_ISR |= STM32_RTC_ISR_INIT;
	while (!(STM32_RTC_ISR & STM32_RTC_ISR_INITF))
		;

	/* Set clock prescalars: Needs two separate writes. */
	STM32_RTC_PRER = (STM32_RTC_PRER & ~STM32_RTC_PRER_S_MASK) |
			 RTC_PREDIV_S;
	STM32_RTC_PRER = (STM32_RTC_PRER & ~STM32_RTC_PRER_A_MASK) |
			 (RTC_PREDIV_A << 16);

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

#ifdef CONFIG_LOW_POWER_IDLE
/* Low power idle statistics */
static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t idle_dsleep_time_us;
static int idle_sleep_prevented_cnt;
static int dsleep_recovery_margin_us = 1000000;

/* STOP_MODE_LATENCY: delay to wake up from STOP mode with main regulator off */
#define STOP_MODE_LATENCY 50 /* us */
/* PLL_LOCK_LATENCY: delay to switch from HSI to PLL */
#define PLL_LOCK_LATENCY 150 /* us */

void low_power_init(void)
{
	/* Turn off the main regulator during stop mode */
	STM32_PWR_CR |= STM32_PWR_CR_LPSDSR /* aka LPDS */;
}

void clock_refresh_console_in_use(void)
{
}

static bool timer_interrupt_pending(void)
{
	return task_is_irq_pending(IRQ_TIM(TIM_CLOCK32));
}

void __idle(void)
{
	timestamp_t t0;
	uint32_t rtc_diff;
	int next_delay, margin_us;
	struct rtc_time_reg rtc0, rtc1, rtc_sleep;

	while (1) {
		interrupt_disable();

		/*
		 * Get timestamp with interrupts disabled.
		 * This value is used as a base to calculate timestamp after
		 * wake from deep sleep. In combination with next_delay it gives
		 * information how long the CPU can sleep. The timestamp can
		 * point to the previous "epoch" when timer overflowed after
		 * interrupts were disabled, since clksrc_high (which keeps
		 * higher 32 bits of the timestamp) will not be updated.
		 */
		rtc_read(&rtc0);
		t0 = get_time();

		/*
		 * Get time to next event.
		 * After disabling interrupts, event timestamp
		 * (__hw_clock_event_get()) is frozen, because
		 * process_timers(), responsible for updating the
		 * next event value with __hw_clock_event_set(),
		 * can't be called. There is a risk that timer overflow
		 * occurred after interrupts were disabled and obtained
		 * event timestamp points to previous "epoch". We will
		 * check that later.
		 */
		next_delay = __hw_clock_event_get() - t0.le.lo;

		/*
		 * Repeat idle enter procedure when timer interrupt is pending
		 * (eg. overflow occurred after disabling interrupts). To work
		 * properly, this code assumes that timer interrupt is enabled
		 * in NVIC and interrupt is generated on timer overflow.
		 */
		if (timer_interrupt_pending()) {
			idle_sleep_prevented_cnt++;

			/* Enable interrupts to handle detected overflow. */
			interrupt_enable();

			/* Repeat idle enter procedure. */
			continue;
		}

		if (DEEP_SLEEP_ALLOWED &&
		    (next_delay > (STOP_MODE_LATENCY + PLL_LOCK_LATENCY +
				   SET_RTC_MATCH_DELAY))) {
			/*
			 * Sleep time MUST be smaller than watchdog period.
			 * Otherwise watchdog will wake us from deep sleep
			 * which is not what we want. Please note that this
			 * assert won't fire if we are already part way through
			 * the watchdog period.
			 */
			ASSERT(next_delay < CONFIG_WATCHDOG_PERIOD_MS * MSEC);

			/* Deep-sleep in STOP mode */
			idle_dsleep_cnt++;

			/*
			 * TODO(b/174337385) no support for wake-up on USART
			 * uart_enable_wakeup(1);
			 */

			/* Set deep sleep bit */
			CPU_SCB_SYSCTRL |= 0x4;

			set_rtc_alarm(0,
				      next_delay - STOP_MODE_LATENCY -
					      PLL_LOCK_LATENCY,
				      &rtc_sleep, 0);

			/* Switch to HSI */
			clock_switch_osc(OSC_HSI);
			/* Turn off the PLL1 to save power */
			clock_enable_osc(OSC_PLL, false);

			/* ensure outstanding memory transactions complete */
			asm volatile("dsb");

			cpu_enter_suspend_mode();

			CPU_SCB_SYSCTRL &= ~0x4;

			/* turn on PLL and wait until it's ready */
			clock_enable_osc(OSC_PLL, true);
			/* Switch to PLL */
			clock_switch_osc(OSC_PLL);

			/*uart_enable_wakeup(0);*/

			/* Fast forward timer according to RTC counter */
			reset_rtc_alarm(&rtc1);
			rtc_diff = get_rtc_diff(&rtc0, &rtc1);
			t0.val = t0.val + rtc_diff;
			force_time(t0);

			/* Record time spent in deep sleep. */
			idle_dsleep_time_us += get_rtc_diff(&rtc_sleep, &rtc1);

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
			cpu_enter_suspend_mode();
		}
		interrupt_enable();
	}
}

/* Print low power idle statistics. */
static int command_idle_stats(int argc, const char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);
	ccprintf("Num idle calls that deep-sleep:      %d\n", idle_dsleep_cnt);
	ccprintf("Time spent in deep-sleep:            %.6llds\n",
		 idle_dsleep_time_us);
	ccprintf("Num of prevented sleep:              %d\n",
		 idle_sleep_prevented_cnt);
	ccprintf("Total time on:                       %.6llds\n", ts.val);
	ccprintf("Deep-sleep closest to wake deadline: %dus\n",
		 dsleep_recovery_margin_us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");
#endif /* CONFIG_LOW_POWER_IDLE */
