/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)

/* High-speed oscillator default is 64 MHz */
#define STM32_HSI_CLOCK 64000000
/* Low-speed oscillator is 32-Khz */
#define STM32_LSI_CLOCK 32000

/*
 * LPTIM is a 16-bit counter clocked by LSI
 * with /4 prescaler (2^2): period 125 us, full range ~8s
 */
#define LPTIM_PRESCALER_LOG2 2
#define LPTIM_PRESCALER BIT(LPTIM_PRESCALER_LOG2)
#define LPTIM_PERIOD_US (SECOND / (STM32_LSI_CLOCK / LPTIM_PRESCALER))

/*
 * PLL1 configuration:
 * CPU freq = VCO / DIVP = HSI / DIVM * DIVN / DIVP
 *          = 64 / 4 * 50 / 2
 *          = 400 Mhz
 * System clock = 400 Mhz
 *  HPRE = /2  => AHB/Timer clock = 200 Mhz
 */
#if !defined(PLL1_DIVM) && !defined(PLL1_DIVN) && !defined(PLL1_DIVP)
#define PLL1_DIVM 4
#define PLL1_DIVN 50
#define PLL1_DIVP 2
#endif
#define PLL1_FREQ (STM32_HSI_CLOCK / PLL1_DIVM * PLL1_DIVN / PLL1_DIVP)

/* Flash latency settings for AHB/ACLK at 64 Mhz and Vcore in VOS1 range */
#define FLASH_ACLK_64MHZ (STM32_FLASH_ACR_WRHIGHFREQ_85MHZ | \
			  (0 << STM32_FLASH_ACR_LATENCY_SHIFT))
/* Flash latency settings for AHB/ACLK at 200 Mhz and Vcore in VOS1 range */
#define FLASH_ACLK_200MHZ (STM32_FLASH_ACR_WRHIGHFREQ_285MHZ | \
			   (2 << STM32_FLASH_ACR_LATENCY_SHIFT))

enum clock_osc {
	OSC_HSI = 0,	/* High-speed internal oscillator */
	OSC_CSI,	/* Multi-speed internal oscillator: NOT IMPLEMENTED */
	OSC_HSE,	/* High-speed external oscillator: NOT IMPLEMENTED */
	OSC_PLL,	/* PLL */
};

static int freq = STM32_HSI_CLOCK;
static int current_osc = OSC_HSI;

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
	volatile uint32_t dummy __attribute__((unused));

	if (bus == BUS_AHB) {
		while (cycles--)
			dummy = STM32_GPIO_IDR(GPIO_A);
	} else { /* APB */
		while (cycles--)
			dummy = STM32_USART_BRR(STM32_USART1_BASE);
	}
}

static void clock_flash_latency(uint32_t target_acr)
{
	STM32_FLASH_ACR(0) = target_acr;
	while (STM32_FLASH_ACR(0) != target_acr)
		;
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
	case OSC_PLL:
		ready = STM32_RCC_CR_PLL1RDY;
		on = STM32_RCC_CR_PLL1ON;
		break;
	default:
		return;
	}

	if (!(STM32_RCC_CR & ready)) {
		STM32_RCC_CR |= on;
		while (!(STM32_RCC_CR & ready))
			;
	}
}

static void clock_switch_osc(enum clock_osc osc)
{
	uint32_t sw;
	uint32_t sws;

	switch (osc) {
	case OSC_HSI:
		sw = STM32_RCC_CFGR_SW_HSI;
		sws = STM32_RCC_CFGR_SWS_HSI;
		break;
	case OSC_PLL:
		sw = STM32_RCC_CFGR_SW_PLL1;
		sws = STM32_RCC_CFGR_SWS_PLL1;
		break;
	default:
		return;
	}

	STM32_RCC_CFGR = sw;
	while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) != sws)
		;
}

static void switch_voltage_scale(uint32_t vos)
{
	STM32_PWR_D3CR &= ~STM32_PWR_D3CR_VOSMASK;
	STM32_PWR_D3CR |= vos;
	while (!(STM32_PWR_D3CR & STM32_PWR_D3CR_VOSRDY))
		;
}

static void clock_set_osc(enum clock_osc osc)
{
	if (osc == current_osc)
		return;

	hook_notify(HOOK_PRE_FREQ_CHANGE);

	switch (osc) {
	case OSC_HSI:
		/* Switch to HSI */
		clock_switch_osc(osc);
		freq = STM32_HSI_CLOCK;
		/* Restore /1 HPRE (AHB prescaler) */
		STM32_RCC_D1CFGR = STM32_RCC_D1CFGR_HPRE_DIV1
				 | STM32_RCC_D1CFGR_D1PPRE_DIV1
				 | STM32_RCC_D1CFGR_D1CPRE_DIV1;
		/* Use more optimized flash latency settings for 64-MHz ACLK */
		clock_flash_latency(FLASH_ACLK_64MHZ);
		/* Turn off the PLL1 to save power */
		STM32_RCC_CR &= ~STM32_RCC_CR_PLL1ON;
		switch_voltage_scale(STM32_PWR_D3CR_VOS3);
		break;

	case OSC_PLL:
		switch_voltage_scale(STM32_PWR_D3CR_VOS1);
		/* Configure PLL1 using 64 Mhz HSI as input */
		STM32_RCC_PLLCKSELR = STM32_RCC_PLLCKSEL_PLLSRC_HSI |
				      STM32_RCC_PLLCKSEL_DIVM1(PLL1_DIVM);
		/* in integer mode, wide range VCO with 16Mhz input, use divP */
		STM32_RCC_PLLCFGR = STM32_RCC_PLLCFG_PLL1VCOSEL_WIDE
				| STM32_RCC_PLLCFG_PLL1RGE_8M_16M
				| STM32_RCC_PLLCFG_DIVP1EN;
		STM32_RCC_PLL1DIVR = STM32_RCC_PLLDIV_DIVP(PLL1_DIVP)
				| STM32_RCC_PLLDIV_DIVN(PLL1_DIVN);
		/* turn on PLL1 and wait that it's ready */
		clock_enable_osc(OSC_PLL);
		/* Put /2 on HPRE (AHB prescaler) to keep at the 200Mhz max */
		STM32_RCC_D1CFGR = STM32_RCC_D1CFGR_HPRE_DIV2
				 | STM32_RCC_D1CFGR_D1PPRE_DIV1
				 | STM32_RCC_D1CFGR_D1CPRE_DIV1;
		freq = PLL1_FREQ / 2;
		/* Increase flash latency before transition the clock */
		clock_flash_latency(FLASH_ACLK_200MHZ);
		/* Switch to PLL */
		clock_switch_osc(OSC_PLL);
		break;
	default:
		break;
	}

	current_osc = osc;
	hook_notify(HOOK_FREQ_CHANGE);
}

void clock_enable_module(enum module_id module, int enable)
{
	/* Assume we have a single task using MODULE_FAST_CPU */
	if (module == MODULE_FAST_CPU) {
		/* the PLL would be off in low power mode, disable it */
		if (enable)
			disable_sleep(SLEEP_MASK_PLL);
		else
			enable_sleep(SLEEP_MASK_PLL);
		clock_set_osc(enable ? OSC_PLL : OSC_HSI);
	}
}

#ifdef CONFIG_LOW_POWER_IDLE
/* Low power idle statistics */
static int idle_sleep_cnt;
static int idle_dsleep_cnt;
static uint64_t idle_dsleep_time_us;
static int dsleep_recovery_margin_us = 1000000;

/* STOP_MODE_LATENCY: delay to wake up from STOP mode with flash off in SVOS5 */
#define STOP_MODE_LATENCY 50 /* us */

static void low_power_init(void)
{
	/* Clock LPTIM1 on the 32-kHz LSI for STOP mode time keeping */
	STM32_RCC_D2CCIP2R = (STM32_RCC_D2CCIP2R &
		~STM32_RCC_D2CCIP2_LPTIM1SEL_MASK)
		| STM32_RCC_D2CCIP2_LPTIM1SEL_LSI;

	/* configure LPTIM1 as our 1-Khz low power timer in STOP mode */
	STM32_RCC_APB1LENR |= STM32_RCC_PB1_LPTIM1;
	STM32_LPTIM_CR(1) = 0; /* ensure it's disabled before configuring */
	STM32_LPTIM_CFGR(1) = LPTIM_PRESCALER_LOG2 << 9; /* Prescaler /4 */
	STM32_LPTIM_IER(1) = STM32_LPTIM_INT_CMPM; /* Compare int for wake-up */
	/* Start the 16-bit free-running counter */
	STM32_LPTIM_CR(1) = STM32_LPTIM_CR_ENABLE;
	STM32_LPTIM_ARR(1) = 0xFFFF;
	STM32_LPTIM_CR(1) = STM32_LPTIM_CR_ENABLE | STM32_LPTIM_CR_CNTSTRT;
	task_enable_irq(STM32_IRQ_LPTIM1);

	/* Wake-up interrupts from EXTI for USART and LPTIM */
	STM32_EXTI_CPUIMR1 |= BIT(26); /* [26] wkup26: USART1 wake-up */
	STM32_EXTI_CPUIMR2 |= BIT(15); /* [15] wkup47: LPTIM1 wake-up */

	/* optimize power vs latency in STOP mode */
	STM32_PWR_CR = (STM32_PWR_CR & ~STM32_PWR_CR_SVOS_MASK)
		     | STM32_PWR_CR_SVOS5
		     | STM32_PWR_CR_FLPS;
}

void clock_refresh_console_in_use(void)
{
}

void lptim_interrupt(void)
{
	STM32_LPTIM_ICR(1) = STM32_LPTIM_INT_CMPM;
}
DECLARE_IRQ(STM32_IRQ_LPTIM1, lptim_interrupt, 2);

static uint16_t lptim_read(void)
{
	uint16_t cnt;

	do {
		cnt = STM32_LPTIM_CNT(1);
	} while (cnt != STM32_LPTIM_CNT(1));

	return cnt;
}

static void set_lptim_event(int delay_us, uint16_t *lptim_cnt)
{
	uint16_t cnt = lptim_read();

	STM32_LPTIM_CMP(1) = cnt + MIN(delay_us / LPTIM_PERIOD_US - 1, 0xffff);
	/* clean-up previous event */
	STM32_LPTIM_ICR(1) = STM32_LPTIM_INT_CMPM;
	*lptim_cnt = cnt;
}

void __idle(void)
{
	timestamp_t t0;
	int next_delay;
	int margin_us, t_diff;
	uint16_t lptim0;

	while (1) {
		asm volatile("cpsid i");

		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		if (DEEP_SLEEP_ALLOWED &&
		    next_delay > LPTIM_PERIOD_US + STOP_MODE_LATENCY) {
			/* deep-sleep in STOP mode */
			idle_dsleep_cnt++;

			uart_enable_wakeup(1);

			/* set deep sleep bit */
			CPU_SCB_SYSCTRL |= 0x4;

			set_lptim_event(next_delay - STOP_MODE_LATENCY,
					&lptim0);

			/* ensure outstanding memory transactions complete */
			asm volatile("dsb");

			asm("wfi");

			CPU_SCB_SYSCTRL &= ~0x4;

			/* fast forward timer according to low power counter */
			if (STM32_PWR_CPUCR & STM32_PWR_CPUCR_STOPF) {
				uint16_t lptim_dt = lptim_read() - lptim0;

				t_diff = (int)lptim_dt * LPTIM_PERIOD_US;
				t0.val = t0.val + t_diff;
				force_time(t0);
				/* clear STOPF flag */
				STM32_PWR_CPUCR |= STM32_PWR_CPUCR_CSSF;
			} else { /* STOP entry was aborted, no fixup */
				t_diff = 0;
			}

			uart_enable_wakeup(0);

			/* Record time spent in deep sleep. */
			idle_dsleep_time_us += t_diff;

			/* Calculate how close we were to missing deadline */
			margin_us = next_delay - t_diff;
			if (margin_us < 0)
				/* Use CPUTS to save stack space */
				CPUTS("Overslept!\n");

			/* Record the closest to missing a deadline. */
			if (margin_us < dsleep_recovery_margin_us)
				dsleep_recovery_margin_us = margin_us;
		} else {
			idle_sleep_cnt++;

			/* normal idle : only CPU clock stopped */
			asm("wfi");
		}
		asm volatile("cpsie i");
	}
}

#ifdef CONFIG_CMD_IDLE_STATS
/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, char **argv)
{
	timestamp_t ts = get_time();

	ccprintf("Num idle calls that sleep:           %d\n", idle_sleep_cnt);
	ccprintf("Num idle calls that deep-sleep:      %d\n", idle_dsleep_cnt);
	ccprintf("Time spent in deep-sleep:            %.6lds\n",
			idle_dsleep_time_us);
	ccprintf("Total time on:                       %.6lds\n", ts.val);
	ccprintf("Deep-sleep closest to wake deadline: %dus\n",
			dsleep_recovery_margin_us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats,
			"",
			"Print last idle stats");
#endif /* CONFIG_CMD_IDLE_STATS */
#endif /* CONFIG_LOW_POWER_IDLE */

void clock_init(void)
{
	/*
	 * STM32H743 Errata 2.2.15:
	 * 'Reading from AXI SRAM might lead to data read corruption'
	 *
	 * limit concurrent read access on AXI master to 1.
	 */
	STM32_AXI_TARG_FN_MOD(7) |= READ_ISS_OVERRIDE;

	/*
	 * Lock (SCUEN=0) power configuration with the LDO enabled.
	 *
	 * The STM32H7 Reference Manual says:
	 * The lower byte of this register is written once after POR and shall
	 * be written before changing VOS level or ck_sys clock frequency.
	 *
	 * The interesting side-effect of this that while the LDO is enabled by
	 * default at startup, if we enter STOP mode without locking it the MCU
	 * seems to freeze forever.
	 */
	STM32_PWR_CR3 = STM32_PWR_CR3_LDOEN;
	/*
	 * Ensure the SPI is always clocked at the same frequency
	 * by putting it on the fixed 64-Mhz HSI clock.
	 * per_ck is clocked directly by the HSI (as per the default settings).
	 */
	STM32_RCC_D2CCIP1R = (STM32_RCC_D2CCIP1R &
		~(STM32_RCC_D2CCIP1R_SPI123SEL_MASK |
		  STM32_RCC_D2CCIP1R_SPI45SEL_MASK))
		| STM32_RCC_D2CCIP1R_SPI123SEL_PERCK
		| STM32_RCC_D2CCIP1R_SPI45SEL_HSI;

	/* Use more optimized flash latency settings for ACLK = HSI = 64 Mhz */
	clock_flash_latency(FLASH_ACLK_64MHZ);

	/* Ensure that LSI is ON to clock LPTIM1 and IWDG */
	STM32_RCC_CSR |= STM32_RCC_CSR_LSION;
	while (!(STM32_RCC_CSR & STM32_RCC_CSR_LSIRDY))
		;

#ifdef CONFIG_LOW_POWER_IDLE
	low_power_init();
#endif
}

static int command_clock(int argc, char **argv)
{
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "hsi"))
			clock_set_osc(OSC_HSI);
		else if (!strcasecmp(argv[1], "pll"))
			clock_set_osc(OSC_PLL);
		else
			return EC_ERROR_PARAM1;
	}
	ccprintf("Clock frequency is now %d Hz\n", freq);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(clock, command_clock,
			"hsi | pll", "Set clock frequency");
