/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings for STM32L5xx. */

#include "builtin/assert.h"
#include "chipset.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "rtc.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)

#define STM32L5_RTC_REQ 1000000
#define STM32L5_LSI_CLOCK 32000

/* High-speed oscillator is 16 MHz */
#define STM32_HSI_CLOCK 16000000
/* Multi-speed oscillator is 4 MHz by default */
#define STM32_MSI_CLOCK 4000000

/* Real Time Clock (RTC) */

#ifdef CONFIG_STM32_CLOCK_HSE_HZ
#define RTC_PREDIV_A 39
#define RTC_FREQ ((STM32L5_RTC_REQ) / (RTC_PREDIV_A + 1)) /* Hz */
#else /* from LSI clock */
#define RTC_PREDIV_A 1
#define RTC_FREQ (STM32L5_LSI_CLOCK / (RTC_PREDIV_A + 1)) /* Hz */
#endif
#define RTC_PREDIV_S (RTC_FREQ - 1)
/*
 * Scaling factor to ensure that the intermediate values computed from/to the
 * RTC frequency are fitting in a 32-bit integer.
 */
#define SCALING 1000

enum clock_osc {
	OSC_INIT = 0, /* Uninitialized */
	OSC_HSI, /* High-speed internal oscillator */
	OSC_MSI, /* Multi-speed internal oscillator */
#ifdef STM32_HSE_CLOCK /* Allows us to catch absence of HSE at comiple time */
	OSC_HSE, /* High-speed external oscillator */
#endif
	OSC_PLL, /* PLL */
};

static int freq = STM32_MSI_CLOCK;
static int current_osc;

int clock_get_freq(void)
{
	return freq;
}

int clock_get_timer_freq(void)
{
	return clock_get_freq();
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

static void clock_enable_osc(enum clock_osc osc)
{
	uint32_t ready;
	uint32_t on;

	switch (osc) {
	case OSC_HSI:
		ready = STM32_RCC_CR_HSIRDY;
		on = STM32_RCC_CR_HSION;
		break;
	case OSC_MSI:
		ready = STM32_RCC_CR_MSIRDY;
		on = STM32_RCC_CR_MSION;
		break;
#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
#ifdef STM32_HSE_BYP
		STM32_RCC_CR |= STM32_RCC_CR_HSEBYP;
#endif
		ready = STM32_RCC_CR_HSERDY;
		on = STM32_RCC_CR_HSEON;
		break;
#endif
	case OSC_PLL:
		ready = STM32_RCC_CR_PLLRDY;
		on = STM32_RCC_CR_PLLON;
		break;
	default:
		return;
	}

	/* Enable HSI and wait for HSI to be ready */
	wait_for_ready(&STM32_RCC_CR, on, ready);
}

/* Switch system clock oscillator */
static void clock_switch_osc(enum clock_osc osc)
{
	uint32_t sw;
	uint32_t sws;
	uint32_t val;

	switch (osc) {
	case OSC_HSI:
		sw = STM32_RCC_CFGR_SW_HSI;
		sws = STM32_RCC_CFGR_SWS_HSI;
		break;
	case OSC_MSI:
		sw = STM32_RCC_CFGR_SW_MSI;
		sws = STM32_RCC_CFGR_SWS_MSI;
		break;
#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
		sw = STM32_RCC_CFGR_SW_HSE;
		sws = STM32_RCC_CFGR_SWS_HSE;
		break;
#endif
	case OSC_PLL:
		sw = STM32_RCC_CFGR_SW_PLL;
		sws = STM32_RCC_CFGR_SWS_PLL;
		break;
	default:
		return;
	}
	val = STM32_RCC_CFGR;
	val &= ~STM32_RCC_CFGR_SW;
	val |= sw;
	STM32_RCC_CFGR = val;
	while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MSK) != sws)
		;
}

/*
 * Configure PLL for HSE
 *
 * 1. Disable the PLL by setting PLLON to 0 in RCC_CR.
 * 2. Wait until PLLRDY is cleared. The PLL is now fully stopped.
 * 3. Change the desired parameter.
 * 4. Enable the PLL again by setting PLLON to 1.
 * 5. Enable the desired PLL outputs by configuring PLLPEN, PLLQEN, PLLREN
 *    in RCC_PLLCFGR.
 */
static int stm32_configure_pll(enum clock_osc osc, uint8_t m, uint8_t n,
			       uint8_t r)
{
	uint32_t val;
	bool pll_unchanged;
	int f;

	val = STM32_RCC_PLLCFGR;
	pll_unchanged = true;

	if (osc == OSC_HSI)
		if ((val & STM32_RCC_PLLCFGR_PLLSRC_MSK) !=
		    STM32_RCC_PLLCFGR_PLLSRC_HSI)
			pll_unchanged = false;

	if (osc == OSC_MSI)
		if ((val & STM32_RCC_PLLCFGR_PLLSRC_MSK) !=
		    STM32_RCC_PLLCFGR_PLLSRC_MSI)
			pll_unchanged = false;

#ifdef STM32_HSE_CLOCK
	if (osc == OSC_HSE)
		if ((val & STM32_RCC_PLLCFGR_PLLSRC_MSK) !=
		    STM32_RCC_PLLCFGR_PLLSRC_HSE)
			pll_unchanged = false;
#endif

	if ((val & STM32_RCC_PLLCFGR_PLLM_MSK) !=
	    ((m - 1) << STM32_RCC_PLLCFGR_PLLM_POS))
		pll_unchanged = false;

	if ((val & STM32_RCC_PLLCFGR_PLLN_MSK) !=
	    (n << STM32_RCC_PLLCFGR_PLLN_POS))
		pll_unchanged = false;

	if ((val & STM32_RCC_PLLCFGR_PLLR_MSK) !=
	    (((r >> 1) - 1) << STM32_RCC_PLLCFGR_PLLR_POS))
		pll_unchanged = false;

	if (pll_unchanged == true) {
		if (osc == OSC_HSI)
			f = STM32_HSI_CLOCK;
		else
			f = STM32_MSI_CLOCK;

		if (!(STM32_RCC_CR & STM32_RCC_CR_PLLRDY)) {
			STM32_RCC_CR |= STM32_RCC_CR_PLLON;
			STM32_RCC_PLLCFGR |= STM32_RCC_PLLCFGR_PLLREN;

			while ((STM32_RCC_CR & STM32_RCC_CR_PLLRDY) == 0)
				;
		}
		/* (f * n) shouldn't overflow based on their max values */
		return (f * n / m / r);
	}
	/* 1 */
	STM32_RCC_CR &= ~STM32_RCC_CR_PLLON;

	/* 2 */
	while (STM32_RCC_CR & STM32_RCC_CR_PLLRDY)
		;

	/* 3 */
	val = STM32_RCC_PLLCFGR;

	val &= ~STM32_RCC_PLLCFGR_PLLSRC_MSK;
	switch (osc) {
	case OSC_HSI:
		val |= STM32_RCC_PLLCFGR_PLLSRC_HSI;
		f = STM32_HSI_CLOCK;
		break;
	case OSC_MSI:
		val |= STM32_RCC_PLLCFGR_PLLSRC_MSI;
		f = STM32_MSI_CLOCK;
		break;
#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
		val |= STM32_RCC_PLLCFGR_PLLSRC_HSE;
		f = STM32_HSE_CLOCK;
		break;
#endif
	default:
		return -1;
	}

	ASSERT(m > 0 && m < 9);
	val &= ~STM32_RCC_PLLCFGR_PLLM_MSK;
	val |= (m - 1) << STM32_RCC_PLLCFGR_PLLM_POS;

	/* Max and min values are from TRM */
	ASSERT(n > 7 && n < 87);
	val &= ~STM32_RCC_PLLCFGR_PLLN_MSK;
	val |= n << STM32_RCC_PLLCFGR_PLLN_POS;

	val &= ~STM32_RCC_PLLCFGR_PLLR_MSK;
	switch (r) {
	case 2:
		val |= 0 << STM32_RCC_PLLCFGR_PLLR_POS;
		break;
	case 4:
		val |= 1 << STM32_RCC_PLLCFGR_PLLR_POS;
		break;
	case 6:
		val |= 2 << STM32_RCC_PLLCFGR_PLLR_POS;
		break;
	case 8:
		val |= 3 << STM32_RCC_PLLCFGR_PLLR_POS;
		break;
	default:
		return -1;
	}

	STM32_RCC_PLLCFGR = val;

	/* 4 */
	clock_enable_osc(OSC_PLL);

	/* 5 */
	val = STM32_RCC_PLLCFGR;
	val |= 1 << STM32_RCC_PLLCFGR_PLLREN_POS;
	STM32_RCC_PLLCFGR = val;

	/* (f * n) shouldn't overflow based on their max values */
	return (f * n / m / r);
}

/**
 * Set system clock oscillator
 *
 * @param osc		Oscillator to use
 * @param pll_osc	Source oscillator for PLL. Ignored if osc is not PLL.
 */
static void clock_set_osc(enum clock_osc osc, enum clock_osc pll_osc)
{
	uint32_t val;

	if (osc == current_osc)
		return;

	if (current_osc != OSC_INIT)
		hook_notify(HOOK_PRE_FREQ_CHANGE);

	switch (osc) {
	case OSC_HSI:
		/* Ensure that HSI is ON */
		clock_enable_osc(osc);

		/* Set HSI as system clock after exiting stop mode */
		STM32_RCC_CFGR |= STM32_RCC_CFGR_STOPWUCK;

		/* Switch to HSI */
		clock_switch_osc(osc);

		/* Disable MSI */
		STM32_RCC_CR &= ~STM32_RCC_CR_MSION;

		freq = STM32_HSI_CLOCK;
		break;

	case OSC_MSI:
		/* Ensure that MSI is ON */
		clock_enable_osc(osc);

		/*
		 * Set MSI as system clock after exiting stop mode
		 */
		STM32_RCC_CFGR &= ~STM32_RCC_CFGR_STOPWUCK;

		/* Switch to MSI */
		clock_switch_osc(osc);

		/* Disable HSI */
		STM32_RCC_CR &= ~STM32_RCC_CR_HSION;

		freq = STM32_MSI_CLOCK;
		break;

#ifdef STM32_HSE_CLOCK
	case OSC_HSE:
		/* Ensure that HSE is stable */
		clock_enable_osc(osc);

		/* Switch to HSE */
		clock_switch_osc(osc);

		/* Disable other clock sources */
		STM32_RCC_CR &= ~(STM32_RCC_CR_MSION | STM32_RCC_CR_HSION |
				  STM32_RCC_CR_PLLON);

		freq = STM32_HSE_CLOCK;

		break;
#endif
	case OSC_PLL:
		/* Ensure that source clock is stable */
		if (pll_osc == OSC_INIT) {
			if ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MSK) !=
			    STM32_RCC_CFGR_SWS_PLL) {
				STM32_RCC_CFGR |= STM32_RCC_CFGR_STOPWUCK;
				clock_enable_osc(OSC_HSI);
				freq = stm32_configure_pll(OSC_HSI, STM32_PLLM,
							   STM32_PLLN,
							   STM32_PLLR);
			} else {
				/* already set PLL, skip */
				freq = STM32_HSI_CLOCK * STM32_PLLN /
				       STM32_PLLM / STM32_PLLR;
				break;
			}
		} else {
			clock_enable_osc(pll_osc);
			/* Configure PLLCFGR */
			freq = stm32_configure_pll(pll_osc, STM32_PLLM,
						   STM32_PLLN, STM32_PLLR);
		}
		ASSERT(freq > 0);

		if (freq > 26000000U) {
			/* Change to Range 0/1 if Freq > 26MHz */
			val = STM32_PWR_CR1;
			val &= ~PWR_CR1_VOS_MSK;
			if (freq > 80000000U) {
				/* Set VCO range 0 */
				val |= PWR_CR1_VOS_RANGE0;
			} else {
				/* Set VCO range 1 */
				val |= PWR_CR1_VOS_RANGE1;
			}
			STM32_PWR_CR1 = val;

			/*
			 * Set Flash latency according to frequency
			 */
			val = STM32_FLASH_ACR;
			val &= ~STM32_FLASH_ACR_LATENCY_MASK;
			if (freq <= 20000000U) {
				/* nothing */
			} else if (freq <= 40000000U) {
				val |= 1;
			} else if (freq <= 60000000U) {
				val |= 2;
			} else if (freq <= 80000000U) {
				val |= 3;
			} else if (freq <= 100000000U) {
				val |= 4;
			} else if (freq <= 110000000U) {
				val |= 5;
			} else {
				val |= 5;
				CPUTS("Incorrect Frequency setting in VOS0!\n");
			}
			STM32_FLASH_ACR = val;
		} else {
			/* Remain in low power Range 2 if Freq <= 26MHz */
			val = STM32_FLASH_ACR;
			val &= ~STM32_FLASH_ACR_LATENCY_MASK;

			if (freq <= 8000000U) {
				/* nothing */
			} else if (freq <= 16000000U) {
				val |= 1;
			} else if (freq <= 26000000U) {
				val |= 2;
			} else {
				val |= 3;
				CPUTS("Incorrect Frequency setting in VOS2!\n");
			}
			STM32_FLASH_ACR = val;
		}

		while (val != STM32_FLASH_ACR)
			;

		/* Switch to PLL */
		clock_switch_osc(osc);

		/* TODO: Disable other sources */
		break;
	default:
		break;
	}

	/* Notify modules of frequency change unless we're initializing */
	if (current_osc != OSC_INIT) {
		current_osc = osc;
		hook_notify(HOOK_FREQ_CHANGE);
	} else {
		current_osc = osc;
	}
}

static uint64_t clock_mask;

test_mockable void clock_enable_module(enum module_id module, int enable)
{
	uint64_t new_mask;

	if (enable)
		new_mask = clock_mask | BIT_ULL(module);
	else
		new_mask = clock_mask & ~BIT_ULL(module);

	/* Only change clock if needed */
	if (new_mask == clock_mask)
		return;

	if (module == MODULE_ADC) {
		STM32_RCC_APB2ENR |= STM32_RCC_PB2_SYSCFGEN;
		STM32_RCC_APB1ENR1 |= STM32_RCC_PB1_PWREN;

		/* ADC select bit 28/29 */
		STM32_RCC_CCIPR &= ~STM32_RCC_CCIPR_ADCSEL_MSK;
		STM32_RCC_CCIPR |=
			(STM32_RCC_CCIPR_ADCSEL_0 | STM32_RCC_CCIPR_ADCSEL_1);
		/* ADC clock enable */
		if (enable)
			STM32_RCC_AHB2ENR |= STM32_RCC_HB2_ADC1;
		else
			STM32_RCC_AHB2ENR &= ~STM32_RCC_HB2_ADC1;
	} else if (module == MODULE_SPI_FLASH) {
		if (enable)
			STM32_RCC_APB1ENR1 |= STM32_RCC_PB1_SPI2;
		else
			STM32_RCC_APB1ENR1 &= ~STM32_RCC_PB1_SPI2;
	} else if (module == MODULE_SPI || module == MODULE_SPI_CONTROLLER) {
		if (enable)
			STM32_RCC_APB2ENR |= STM32_RCC_APB2ENR_SPI1EN;
		else if ((new_mask &
			  (BIT(MODULE_SPI) | BIT(MODULE_SPI_CONTROLLER))) == 0)
			STM32_RCC_APB2ENR &= ~STM32_RCC_APB2ENR_SPI1EN;
	} else if (module == MODULE_USB) {
		if (enable) {
			/* Keep USB subsystem under reset for now. */
			STM32_RCC_APB1RSTR2 |= STM32_RCC_APB1RSTR2_USBFSRST;

			/* Enable power to the USB domain. */
			STM32_PWR_CR2 |= STM32_PWR_CR2_USV;

			/* Enable internal 48 MHz RC oscillator. */
			wait_for_ready(&STM32_RCC_CRRCR,
				       STM32_RCC_CRRCR_HSI48ON,
				       STM32_RCC_CRRCR_HSI48RDY);

			/* Enable USB device clock. */
			STM32_RCC_APB1ENR2 |= STM32_RCC_APB1ENR2_USBFSEN;

			/* 48 MHz clock is stable, release USB reset. */
			STM32_RCC_APB1RSTR2 &= ~STM32_RCC_APB1RSTR2_USBFSRST;

		} else {
			STM32_RCC_APB1ENR2 &= ~STM32_RCC_APB1ENR2_USBFSEN;
			STM32_CRS_CR &=
				~(STM32_CRS_CR_CEN | STM32_CRS_CR_AUTOTRIMEN);
			STM32_RCC_CRRCR &= ~STM32_RCC_CRRCR_HSI48ON;
			STM32_PWR_CR2 &= ~STM32_PWR_CR2_USV;
		}
	}

	clock_mask = new_mask;
}

int clock_is_module_enabled(enum module_id module)
{
	return !!(clock_mask & BIT_ULL(module));
}

void rtc_init(void)
{
	/* Enable RTC Alarm in EXTI */
	STM32_EXTI_RTSR |= EXTI_RTC_ALR_EVENT;
	task_enable_irq(STM32_IRQ_RTC_ALARM);

	/* RTC was initilaized, avoid initialization again */
	if (STM32_RTC_ISR & STM32_RTC_ISR_INITS)
		return;

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

void clock_init(void)
{
#ifdef STM32_HSE_CLOCK
	clock_set_osc(OSC_PLL, OSC_HSE);
#else
#ifdef STM32_USE_PLL
	clock_set_osc(OSC_PLL, OSC_INIT);
#else
	clock_set_osc(OSC_HSI, OSC_INIT);
#endif
#endif

#ifdef CONFIG_LOW_POWER_IDLE
	low_power_init();
	rtc_init();
#endif
}

static void clock_chipset_startup(void)
{
	/* Return to full speed */
	clock_enable_module(MODULE_CHIPSET, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, clock_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, clock_chipset_startup, HOOK_PRIO_DEFAULT);

static void clock_chipset_shutdown(void)
{
	/* Drop to lower clock speed if no other module requires full speed */
	clock_enable_module(MODULE_CHIPSET, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, clock_chipset_shutdown, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, clock_chipset_shutdown, HOOK_PRIO_DEFAULT);

static int command_clock(int argc, const char **argv)
{
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "hsi"))
			clock_set_osc(OSC_HSI, OSC_INIT);
		else if (!strcasecmp(argv[1], "msi"))
			clock_set_osc(OSC_MSI, OSC_INIT);
#ifdef STM32_HSE_CLOCK
		else if (!strcasecmp(argv[1], "hse"))
			clock_set_osc(OSC_HSE, OSC_INIT);
		else if (!strcasecmp(argv[1], "pll"))
			clock_set_osc(OSC_PLL, OSC_HSE);
#else
		else if (!strcasecmp(argv[1], "pll"))
			clock_set_osc(OSC_PLL, OSC_HSI);
#endif
		else
			return EC_ERROR_PARAM1;
	}

	ccprintf("Clock frequency is now %d Hz\n", freq);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(clock, command_clock,
			"hsi | msi"
#ifdef STM32_HSE_CLOCK
			" | hse"
#endif
			" | pll",
			"Set clock frequency");

uint32_t rtcss_to_us(uint32_t rtcss)
{
	return ((RTC_PREDIV_S - (rtcss & 0x7FFF)) * (SECOND / SCALING) /
		(RTC_FREQ / SCALING));
}

uint32_t us_to_rtcss(uint32_t us)
{
	return (RTC_PREDIV_S -
		(us * (RTC_FREQ / SCALING) / (SECOND / SCALING)));
}

/* Convert decimal to BCD */
static uint8_t u8_to_bcd(uint8_t val)
{
	/* Fast division by 10 (when lacking HW div) */
	uint32_t quot = ((uint32_t)val * 0xCCCD) >> 19;
	uint32_t rem = val - quot * 10;

	return rem | (quot << 4);
}

/* Convert between RTC regs in BCD and seconds */
static uint32_t rtc_tr_to_sec(uint32_t rtc_tr)
{
	uint32_t sec;

	/* convert the hours field */
	sec = (((rtc_tr & RTC_TR_HT) >> RTC_TR_HT_POS) * 10 +
	       ((rtc_tr & RTC_TR_HU) >> RTC_TR_HU_POS)) *
	      3600;
	/* convert the minutes field */
	sec += (((rtc_tr & RTC_TR_MNT) >> RTC_TR_MNT_POS) * 10 +
		((rtc_tr & RTC_TR_MNU) >> RTC_TR_MNU_POS)) *
	       60;
	/* convert the seconds field */
	sec += ((rtc_tr & RTC_TR_ST) >> RTC_TR_ST_POS) * 10 +
	       (rtc_tr & RTC_TR_SU);
	return sec;
}

static uint32_t sec_to_rtc_tr(uint32_t sec)
{
	uint32_t rtc_tr;
	uint8_t hour;
	uint8_t min;

	sec %= SECS_PER_DAY;
	/* convert the hours field */
	hour = sec / 3600;
	rtc_tr = u8_to_bcd(hour) << 16;
	/* convert the minutes field */
	sec -= hour * 3600;
	min = sec / 60;
	rtc_tr |= u8_to_bcd(min) << 8;
	/* convert the seconds field */
	sec -= min * 60;
	rtc_tr |= u8_to_bcd(sec);

	return rtc_tr;
}

/* Register setup before RTC alarm is allowed for update */
static void pre_work_set_rtc_alarm(void)
{
	rtc_unlock_regs();

	/* Make sure alarm is disabled */
	STM32_RTC_CR &= ~STM32_RTC_CR_ALRAE;
	while (!(STM32_RTC_ISR & STM32_RTC_ISR_ALRAWF))
		;
	STM32_RTC_ISR &= ~STM32_RTC_ISR_ALRAF;
#ifdef STM32_EXTI_RPR
	/* Separate rising and falling edge pending registers. */
	STM32_EXTI_RPR = BIT(18);
	STM32_EXTI_FPR = BIT(18);
#else
	/* One combined rising/falling edge pending registers. */
	STM32_EXTI_PR = BIT(18);
#endif
}

/* Register setup after RTC alarm is updated */
static void post_work_set_rtc_alarm(void)
{
	/* Enable alarm and alarm interrupt */
	STM32_EXTI_IMR |= BIT(18);
	STM32_EXTI_RTSR |= BIT(18);
	STM32_RTC_CR |= (STM32_RTC_CR_ALRAE);

	rtc_lock_regs();
}

#ifdef CONFIG_HOSTCMD_RTC
static struct wake_time host_wake_time;

bool is_host_wake_alarm_expired(timestamp_t ts)
{
	return host_wake_time.ts.val &&
	       timestamp_expired(host_wake_time.ts, &ts);
}

void restore_host_wake_alarm(void)
{
	if (!host_wake_time.ts.val)
		return;

	pre_work_set_rtc_alarm();

	/* Set alarm time */
	STM32_RTC_ALRMAR = host_wake_time.rtc_alrmar;

	post_work_set_rtc_alarm();
}

static uint32_t rtc_dr_to_sec(uint32_t rtc_dr)
{
	struct calendar_date time;
	uint32_t sec;

	time.year =
		(((rtc_dr & 0xf00000) >> 20) * 10 + ((rtc_dr & 0xf0000) >> 16));
	time.month = (((rtc_dr & 0x1000) >> 12) * 10 + ((rtc_dr & 0xf00) >> 8));
	time.day = ((rtc_dr & 0x30) >> 4) * 10 + (rtc_dr & 0xf);

	sec = date_to_sec(time);

	return sec;
}

static uint32_t sec_to_rtc_dr(uint32_t sec)
{
	struct calendar_date time;
	uint32_t rtc_dr;

	time = sec_to_date(sec);

	rtc_dr = u8_to_bcd(time.year) << 16;
	rtc_dr |= u8_to_bcd(time.month) << 8;
	rtc_dr |= u8_to_bcd(time.day);

	return rtc_dr;
}
#endif

uint32_t rtc_to_sec(const struct rtc_time_reg *rtc)
{
	uint32_t sec = 0;

#ifdef CONFIG_HOSTCMD_RTC
	sec = rtc_dr_to_sec(rtc->rtc_dr);
#endif
	return sec + (rtcss_to_us(rtc->rtc_ssr) / SECOND) +
	       rtc_tr_to_sec(rtc->rtc_tr);
}

void sec_to_rtc(uint32_t sec, struct rtc_time_reg *rtc)
{
	rtc->rtc_dr = 0;
#ifdef CONFIG_HOSTCMD_RTC
	rtc->rtc_dr = sec_to_rtc_dr(sec);
#endif
	rtc->rtc_tr = sec_to_rtc_tr(sec);
	rtc->rtc_ssr = 0;
}

/* Return sub-10-sec time diff between two rtc readings
 *
 * Note: this function assumes rtc0 was sampled before rtc1.
 * Additionally, this function only looks at the difference mod 10
 * seconds.
 */
uint32_t get_rtc_diff(const struct rtc_time_reg *rtc0,
		      const struct rtc_time_reg *rtc1)
{
	uint32_t rtc0_val, rtc1_val, diff;

	rtc0_val = (rtc0->rtc_tr & RTC_TR_SU) * SECOND +
		   rtcss_to_us(rtc0->rtc_ssr);
	rtc1_val = (rtc1->rtc_tr & RTC_TR_SU) * SECOND +
		   rtcss_to_us(rtc1->rtc_ssr);
	diff = rtc1_val;
	if (rtc1_val < rtc0_val) {
		/* rtc_ssr has wrapped, since we assume rtc0 < rtc1, add
		 * 10 seconds to get the correct value
		 */
		diff += 10 * SECOND;
	}
	diff -= rtc0_val;
	return diff;
}

void rtc_read(struct rtc_time_reg *rtc)
{
	/*
	 * Read current time synchronously. Each register must be read
	 * twice with identical values because glitches may occur for reads
	 * close to the RTCCLK edge.
	 */
	do {
		rtc->rtc_dr = STM32_RTC_DR;

		do {
			rtc->rtc_tr = STM32_RTC_TR;

			do {
				rtc->rtc_ssr = STM32_RTC_SSR;
			} while (rtc->rtc_ssr != STM32_RTC_SSR);

		} while (rtc->rtc_tr != STM32_RTC_TR);

	} while (rtc->rtc_dr != STM32_RTC_DR);
}

void set_rtc_alarm(uint32_t delay_s, uint32_t delay_us,
		   struct rtc_time_reg *rtc, uint8_t save_alarm)
{
	uint32_t alarm_sec = 0;
	uint32_t alarm_us = 0;

	if (delay_s == EC_RTC_ALARM_CLEAR && !delay_us) {
		reset_rtc_alarm(rtc);
		return;
	}

	/* Alarm timeout must be within 1 day (86400 seconds) */
	ASSERT((delay_s + delay_us / SECOND) < SECS_PER_DAY);

	pre_work_set_rtc_alarm();
	rtc_read(rtc);

	/* Calculate alarm time */
	alarm_sec = rtc_tr_to_sec(rtc->rtc_tr) + delay_s;

	if (delay_us) {
		alarm_us = rtcss_to_us(rtc->rtc_ssr) + delay_us;
		alarm_sec = alarm_sec + alarm_us / SECOND;
		alarm_us = alarm_us % SECOND;
	}

	/*
	 * If seconds is greater than 1 day, subtract by 1 day to deal with
	 * 24-hour rollover.
	 */
	if (alarm_sec >= SECS_PER_DAY)
		alarm_sec -= SECS_PER_DAY;

	/*
	 * Set alarm time in seconds and check for match on
	 * hours, minutes, and seconds.
	 */
	STM32_RTC_ALRMAR = sec_to_rtc_tr(alarm_sec) | 0xc0000000;

	/*
	 * Set alarm time in subseconds and check for match on subseconds.
	 * If the caller doesn't specify subsecond delay (e.g. host command),
	 * just align the alarm time to second.
	 */
	STM32_RTC_ALRMASSR = delay_us ? (us_to_rtcss(alarm_us) | 0x0f000000) :
					0;

#ifdef CONFIG_HOSTCMD_RTC
	/*
	 * If alarm is set by the host, preserve the wake time timestamp
	 * and alarm registers.
	 */
	if (save_alarm) {
		host_wake_time.ts.val = delay_s * SECOND + get_time().val;
		host_wake_time.rtc_alrmar = STM32_RTC_ALRMAR;
	}
#endif
	post_work_set_rtc_alarm();
}

uint32_t get_rtc_alarm(void)
{
	struct rtc_time_reg now;
	uint32_t now_sec;
	uint32_t alarm_sec;

	if (!(STM32_RTC_CR & STM32_RTC_CR_ALRAE))
		return 0;

	rtc_read(&now);

	now_sec = rtc_tr_to_sec(now.rtc_tr);
	alarm_sec = rtc_tr_to_sec(STM32_RTC_ALRMAR & 0x3fffff);

	return ((alarm_sec < now_sec) ? SECS_PER_DAY : 0) +
	       (alarm_sec - now_sec);
}

void reset_rtc_alarm(struct rtc_time_reg *rtc)
{
	rtc_unlock_regs();

	/* Disable alarm */
	STM32_RTC_CR &= ~STM32_RTC_CR_ALRAE;
	STM32_RTC_ISR &= ~STM32_RTC_ISR_ALRAF;

	/* Disable RTC alarm interrupt */
	STM32_EXTI_IMR &= ~BIT(18);
#ifdef STM32_EXTI_RPR
	/* Separate rising and falling edge pending registers. */
	STM32_EXTI_RPR = BIT(18);
	STM32_EXTI_FPR = BIT(18);
#else
	/* One combined rising/falling edge pending registers. */
	STM32_EXTI_PR = BIT(18);
#endif

	/* Clear the pending RTC alarm IRQ in NVIC */
	task_clear_pending_irq(STM32_IRQ_RTC_ALARM);

	/* Read current time */
	rtc_read(rtc);

	rtc_lock_regs();
}

#ifdef CONFIG_HOSTCMD_RTC
static void set_rtc_host_event(void)
{
	host_set_single_event(EC_HOST_EVENT_RTC);
}
DECLARE_DEFERRED(set_rtc_host_event);
#endif

test_mockable_static void __rtc_alarm_irq(void)
{
	struct rtc_time_reg rtc;

	reset_rtc_alarm(&rtc);

#ifdef CONFIG_HOSTCMD_RTC
	/* Wake up the host if there is a saved rtc wake alarm. */
	if (host_wake_time.ts.val) {
		host_wake_time.ts.val = 0;
		hook_call_deferred(&set_rtc_host_event_data, 0);
	}
#endif
}
DECLARE_IRQ(STM32_IRQ_RTC_ALARM, __rtc_alarm_irq, 1);

void print_system_rtc(enum console_channel ch)
{
	uint32_t sec;
	struct rtc_time_reg rtc;

	rtc_read(&rtc);
	sec = rtc_to_sec(&rtc);

	cprintf(ch, "RTC: 0x%08x (%d.00 s)\n", sec, sec);
}

#ifdef CONFIG_LOW_POWER_IDLE
/* Low power idle statistics */
static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t idle_dsleep_time_us;
static int dsleep_recovery_margin_us = 1000000;

/* STOP_MODE_LATENCY: delay to wake up from STOP mode with main regulator off */
#define STOP_MODE_LATENCY 50 /* us */
/* PLL_LOCK_LATENCY: delay to switch from HSI to PLL */
#define PLL_LOCK_LATENCY 150 /* us */
/*
 * SET_RTC_MATCH_DELAY: max time to set RTC match alarm. If we set the alarm
 * in the past, it will never wake up and cause a watchdog.
 */
#define SET_RTC_MATCH_DELAY 120 /* us */

void low_power_init(void)
{
	/* Enter stop1 mode */
	uint32_t val;

	val = STM32_PWR_CR1;
	val &= ~PWR_CR1_LPMS_MSK;
	val |= PWR_CR1_LPMS_STOP1;
	STM32_PWR_CR1 = val;
}

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

		if (DEEP_SLEEP_ALLOWED &&
		    (next_delay > (STOP_MODE_LATENCY + PLL_LOCK_LATENCY +
				   SET_RTC_MATCH_DELAY))) {
			/* Deep-sleep in STOP mode */
			idle_dsleep_cnt++;

			uart_enable_wakeup(1);

			/* Set deep sleep bit */
			CPU_SCB_SYSCTRL |= 0x4;

			set_rtc_alarm(0,
				      next_delay - STOP_MODE_LATENCY -
					      PLL_LOCK_LATENCY,
				      &rtc0, 0);

			/* ensure outstanding memory transactions complete */
			asm volatile("dsb");

			cpu_enter_suspend_mode();

			CPU_SCB_SYSCTRL &= ~0x4;

			/* turn on PLL and wait until it's ready */
			STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_PWREN;
			clock_wait_bus_cycles(BUS_APB, 2);

			stm32_configure_pll(OSC_HSI, STM32_PLLM, STM32_PLLN,
					    STM32_PLLR);

			/* Switch to PLL */
			clock_switch_osc(OSC_PLL);

			uart_enable_wakeup(0);

			/* Fast forward timer according to RTC counter */
			reset_rtc_alarm(&rtc1);
			rtc_diff = get_rtc_diff(&rtc0, &rtc1);
			t0.val = t0.val + rtc_diff;
			force_time(t0);

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
			cpu_enter_suspend_mode();
		}
		interrupt_enable();
	}
}

/*****************************************************************************/
/* Console commands */
/* Print low power idle statistics. */
static int command_idle_stats(int argc, const char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);
	ccprintf("Num idle calls that deep-sleep:      %d\n", idle_dsleep_cnt);
	ccprintf("Time spent in deep-sleep:            %.6llus\n",
		 idle_dsleep_time_us);
	ccprintf("Total time on:                       %.6llus\n", ts.val);
	ccprintf("Deep-sleep closest to wake deadline: %dus\n",
		 dsleep_recovery_margin_us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");
#endif /* CONFIG_LOW_POWER_IDLE */
