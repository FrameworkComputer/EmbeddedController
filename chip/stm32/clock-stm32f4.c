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

#ifdef CONFIG_STM32_CLOCK_HSE_HZ
#define RTC_PREDIV_A 39
#define RTC_FREQ ((STM32F4_RTC_REQ) / (RTC_PREDIV_A + 1)) /* Hz */
#else
/* LSI clock is 40kHz-ish */
#define RTC_PREDIV_A 1
#define RTC_FREQ (40000 / (RTC_PREDIV_A + 1)) /* Hz */
#endif
#define RTC_PREDIV_S (RTC_FREQ - 1)
#define US_PER_RTC_TICK (1000000 / RTC_FREQ)

int32_t rtcss_to_us(uint32_t rtcss)
{
	return ((RTC_PREDIV_S - rtcss) * US_PER_RTC_TICK);
}

uint32_t us_to_rtcss(int32_t us)
{
	return (RTC_PREDIV_S - (us / US_PER_RTC_TICK));
}

void config_hispeed_clock(void)
{
#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	int srcclock = CONFIG_STM32_CLOCK_HSE_HZ;
	int clk_check_mask = STM32_RCC_CR_HSERDY;
	int clk_enable_mask = STM32_RCC_CR_HSEON;
#else
	int srcclock = STM32F4_HSI_CLOCK;
	int clk_check_mask = STM32_RCC_CR_HSIRDY;
	int clk_enable_mask = STM32_RCC_CR_HSION;
#endif
	int plldiv, pllinputclock;
	int pllmult, vcoclock;
	int systemclock;
	int usbdiv;
	int i2sdiv;

	int ahbpre, apb1pre, apb2pre;
	int rtcdiv = 0;

	/* If PLL is the clock source, PLL has already been set up. */
	if ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) ==
	    STM32_RCC_CFGR_SWS_PLL)
		return;

	/* Ensure that HSE/HSI is ON */
	wait_for_ready(&(STM32_RCC_CR), clk_enable_mask, clk_check_mask);

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

	/* All IO clocks at STM32F4_IO_CLOCK
	 * For STM32F446: max 45 MHz
	 * For STM32F412: max 50 MHz
	 */
	/* AHB Prescalar */
	ahbpre = STM32F4_AHB_PRE;
	/* NOTE: If apbXpre is not 0, timers are x2 clocked. RM0390 Fig. 13
	 * One should define STM32F4_TIMER_CLOCK when apbXpre is not 0.
	 * STM32F4_TIMER_CLOCK is used for hwtimer in EC. */
	apb1pre = STM32F4_APB1_PRE;
	apb2pre = STM32F4_APB2_PRE;

#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	/* RTC clock = 1MHz */
	rtcdiv = (CONFIG_STM32_CLOCK_HSE_HZ + (STM32F4_RTC_REQ / 2))
		/ STM32F4_RTC_REQ;
#endif
	/* Switch SYSCLK to PLL, setup prescalars.
	 * EC codebase doesn't understand multiple clock domains
	 * so we enforce a clock config that keeps AHB = APB1 = APB2
	 * allowing ec codebase assumptions about consistent clock
	 * rates to remain true.
	 *
	 * NOTE: Sweetberry requires MCO2 <- HSE @ 24MHz
	 * MCO outputs are selected here but are not changeable later.
	 * A CONFIG may be needed if other boards have different MCO
	 * requirements.
	 */
	STM32_RCC_CFGR =
		(2 << 30) |  /* MCO2 <- HSE */
		(0 << 27) |  /* MCO2 div / 4 */
		(6 << 24) |  /* MCO1 div / 4 */
		(3 << 21) |  /* MCO1 <- PLL */
		CFGR_RTCPRE(rtcdiv) |
		CFGR_PPRE2(apb2pre) |
		CFGR_PPRE1(apb1pre) |
		CFGR_HPRE(ahbpre) |
		STM32_RCC_CFGR_SW_PLL;

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

	wait_for_ready(&(STM32_RCC_CR),
		STM32_RCC_CR_PLLON, STM32_RCC_CR_PLLRDY);

	/* Wait until the PLL is the clock source */
	if ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) ==
	    STM32_RCC_CFGR_SWS_PLL)
		;

	/* Setup RTC Clock input */
#ifdef CONFIG_STM32_CLOCK_HSE_HZ
	STM32_RCC_BDCR = STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC_HSE);
#else
	/* Ensure that LSI is ON */
	wait_for_ready(&(STM32_RCC_CSR),
		STM32_RCC_CSR_LSION, STM32_RCC_CSR_LSIRDY);

	STM32_RCC_BDCR = STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC_LSI);
#endif
}

int clock_get_timer_freq(void)
{
	return STM32F4_TIMER_CLOCK;
}

int clock_get_freq(void)
{
	return STM32F4_IO_CLOCK;
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

void rtc_init(void)
{
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
