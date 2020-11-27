/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "chipset.h"
#include "clock.h"
#include "clock-f.h"
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
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)

enum clock_osc {
	OSC_HSI = 0,	/* High-speed internal oscillator */
	OSC_HSE,	/* High-speed external oscillator */
	OSC_PLL,	/* PLL */
};

/*
 * NOTE: Sweetberry requires MCO2 <- HSE @ 24MHz
 * MCO outputs are selected here but are not changeable later.
 * A CONFIG may be needed if other boards have different MCO
 * requirements.
 */
#define RCC_CFGR_MCO_CONFIG ((2 << 30) | /* MCO2 <- HSE  */ \
			     (0 << 27) | /* MCO2 div / 4 */ \
			     (6 << 24) | /* MCO1 div / 4 */ \
			     (3 << 21))  /* MCO1 <- PLL  */

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
#define RCC_CFGR_DIVIDERS_WITH_PLL (RCC_CFGR_MCO_CONFIG  | \
				    CFGR_RTCPRE(RTC_DIV) | \
				    CFGR_PPRE2(STM32F4_APB2_PRE) | \
				    CFGR_PPRE1(STM32F4_APB1_PRE) | \
				    CFGR_HPRE(STM32F4_AHB_PRE))
/*
 * lower power configuration without the PLL
 * the frequency will be low (8-24Mhz), we don't want dividers to the
 * peripheral clocks, put /1 everywhere.
 */
#define RCC_CFGR_DIVIDERS_NO_PLL (RCC_CFGR_MCO_CONFIG | CFGR_RTCPRE(0) | \
				  CFGR_PPRE2(0) | CFGR_PPRE1(0) | CFGR_HPRE(0))

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
		STM32_FLASH_ACR = (STM32_FLASH_ACR & ~STM32_FLASH_ACR_LAT_MASK)
				| STM32_FLASH_ACR_LATENCY_SLOW;
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
		STM32_FLASH_ACR = (STM32_FLASH_ACR & ~STM32_FLASH_ACR_LAT_MASK)
				| STM32_FLASH_ACR_LATENCY_SLOW;
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
		STM32_FLASH_ACR = (STM32_FLASH_ACR & ~STM32_FLASH_ACR_LAT_MASK)
				| STM32_FLASH_ACR_LATENCY;
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
	STM32_RCC_PLLCFGR =
		PLLCFGR_PLLM(plldiv) |
		PLLCFGR_PLLN(pllmult) |
		PLLCFGR_PLLP(STM32F4_PLLP_DIV / 2 - 1) |
#if defined(CONFIG_STM32_CLOCK_HSE_HZ)
		PLLCFGR_PLLSRC_HSE |
#else
		PLLCFGR_PLLSRC_HSI |
#endif
		PLLCFGR_PLLQ(usbdiv) |
		PLLCFGR_PLLR(i2sdiv);
}

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
	/* we cannot go to low power mode as we are running on the PLL */
	disable_sleep(SLEEP_MASK_PLL);
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

void clock_enable_module(enum module_id module, int enable)
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
				STM32_RCC_I2C1EN | STM32_RCC_I2C2EN
				| STM32_RCC_I2C3EN | STM32_RCC_FMPI2C4EN;
			STM32_RCC_DCKCFGR2 =
				(STM32_RCC_DCKCFGR2 & ~DCKCFGR2_FMPI2C1SEL_MASK)
				| DCKCFGR2_FMPI2C1SEL(FMPI2C1SEL_APB);
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

int32_t rtcss_to_us(uint32_t rtcss)
{
	return ((RTC_PREDIV_S - rtcss) * (SECOND/SCALING) / (RTC_FREQ/SCALING));
}

uint32_t us_to_rtcss(int32_t us)
{
	return (RTC_PREDIV_S - (us * (RTC_FREQ/SCALING) / (SECOND/SCALING)));
}

void rtc_init(void)
{
	/* Setup RTC Clock input */
#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	/* RTC clocked from the HSE */
	STM32_RCC_BDCR = STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC_HSE);
#else
	/* RTC clocked from the LSI, ensure first it is ON */
	wait_for_ready(&(STM32_RCC_CSR),
		STM32_RCC_CSR_LSION, STM32_RCC_CSR_LSIRDY);

	STM32_RCC_BDCR = STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC_LSI);
#endif

	rtc_unlock_regs();

	/* Enter RTC initialize mode */
	STM32_RTC_ISR |= STM32_RTC_ISR_INIT;
	while (!(STM32_RTC_ISR & STM32_RTC_ISR_INITF))
		;

	/* Set clock prescalars: Needs two separate writes. */
	STM32_RTC_PRER =
		(STM32_RTC_PRER & ~STM32_RTC_PRER_S_MASK) | RTC_PREDIV_S;
	STM32_RTC_PRER =
		(STM32_RTC_PRER & ~STM32_RTC_PRER_A_MASK)
		| (RTC_PREDIV_A << 16);

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
