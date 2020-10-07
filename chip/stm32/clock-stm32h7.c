/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Clocks and power management settings
 *
 * Error Handling and Unimplemented Features:
 * Since we are dealing with code critical to the runtime of the CPU,
 * our strategy for unimplemented functionality is to ASSERT, but fallback
 * to doing nothing if ASSERT is not enabled. This is not a perfect solution,
 * but at least yields predictable behavior.
 */


#include <stdbool.h>

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

/* Check chip family and variant for compatibility */
#ifndef CHIP_FAMILY_STM32H7
#error  Source clock-stm32h7.c does not support this chip family.
#endif
#ifndef CHIP_VARIANT_STM32H7X3
#error Unsupported chip variant.
#endif

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)

enum clock_osc {
	OSC_HSI = 0,	/* High-speed internal oscillator */
	OSC_CSI,	/* Multi-speed internal oscillator: NOT IMPLEMENTED */
	OSC_HSE,	/* High-speed external oscillator: NOT IMPLEMENTED */
	OSC_PLL,	/* PLL */
};

enum voltage_scale {
	VOLTAGE_SCALE0 = 0,
	VOLTAGE_SCALE1,
	VOLTAGE_SCALE2,
	VOLTAGE_SCALE3,
	VOLTAGE_SCALE_COUNT,
};

enum freq {
	FREQ_1KHZ   = 1000,
	FREQ_32KHZ  = 32  * FREQ_1KHZ,
	FREQ_1MHZ   = 1000000,
	FREQ_2MHZ   = 2   * FREQ_1MHZ,
	FREQ_16MHZ  = 16  * FREQ_1MHZ,
	FREQ_64MHZ  = 64  * FREQ_1MHZ,
	FREQ_140MHZ = 140 * FREQ_1MHZ,
	FREQ_200MHZ = 200 * FREQ_1MHZ,
	FREQ_280MHZ = 280 * FREQ_1MHZ,
	FREQ_400MHZ = 400 * FREQ_1MHZ,
	FREQ_480MHZ = 480 * FREQ_1MHZ,
};

/* High-speed oscillator default is 64 MHz */
#define STM32_HSI_CLOCK FREQ_64MHZ
/* Low-speed oscillator is 32-Khz */
#define STM32_LSI_CLOCK FREQ_32KHZ

/*
 * LPTIM is a 16-bit counter clocked by LSI
 * with /4 prescaler (2^2): period 125 us, full range ~8s
 */
#define LPTIM_PRESCALER_LOG2 2
/*
 * LPTIM_PRESCALER and LPTIM_PERIOD_US have to be signed, because they
 * determine the signedness of the comparison with |next_delay| in
 * __idle(), where |next_delay| is negative if no next event.
 */
#define LPTIM_PRESCALER ((int)BIT(LPTIM_PRESCALER_LOG2))
#define LPTIM_PERIOD_US (SECOND / (STM32_LSI_CLOCK / LPTIM_PRESCALER))

/* This is not the core frequency */
static enum freq current_bus_freq = STM32_HSI_CLOCK;
static int current_osc = OSC_HSI;

int clock_get_freq(void)
{
	return current_bus_freq;
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
			unused = STM32_GPIO_IDR(GPIO_A);
	} else { /* APB */
		while (cycles--)
			unused = STM32_USART_BRR(STM32_USART1_BASE);
	}
}

/* Flash latency values are dependent on peripheral speed and voltage scale */
static void clock_flash_latency(enum freq axi_freq, enum voltage_scale vos)
{
	uint32_t target_acr;

	if (axi_freq == FREQ_64MHZ && vos == VOLTAGE_SCALE3) {
		target_acr = STM32_FLASH_ACR_WRHIGHFREQ_85MHZ |
			     (0 << STM32_FLASH_ACR_LATENCY_SHIFT);
	} else if (axi_freq == FREQ_200MHZ && vos == VOLTAGE_SCALE1) {
		target_acr = STM32_FLASH_ACR_WRHIGHFREQ_285MHZ |
			     (2 << STM32_FLASH_ACR_LATENCY_SHIFT);
	} else {
		ASSERT(0);
		return;
	}

	STM32_FLASH_ACR(0) = target_acr;
	while (STM32_FLASH_ACR(0) != target_acr)
		;
}

/**
 * @brief Configure PLL1 to output the specified frequency.
 *
 * The input frequency to PLL1 is assumed to be the HSI, which
 * is 64MHz.
 *
 * @param output_freq The target output frequency.
 */
static void clock_pll1_configure(enum freq output_freq) {
	uint32_t divm = 4; // Input prescaler (16MHz max for PLL -- 64/4 ==> 16)
	uint32_t divn;     // Pll multiplier
	uint32_t divp;     // Output 1 prescaler

	switch (output_freq)
	{
	case FREQ_400MHZ:
		/*
		 * PLL1 configuration:
		 * CPU freq = VCO / DIVP = HSI / DIVM * DIVN / DIVP
		 *          = 64MHz/4 * 50 / 2
		 *          = 16MHz * 50 / 2
		 *          = 400 Mhz
		 */
		divn = 50;
		divp = 2;
		break;
	case FREQ_200MHZ:
		/*
		 * PLL1 configuration:
		 * CPU freq = VCO / DIVP = HSI / DIVM * DIVN / DIVP
		 *          = 64 / 4 * 25 / 2
		 *          = 16MHz * 25 / 2
		 *          = 200 Mhz
		 */
		divn = 25;
		divp = 2;
		break;
	case FREQ_280MHZ:
		divn = 35;
		divp = 2;
		break;
	case FREQ_480MHZ:
		divn = 60;
		divp = 2;
		break;
	default:
		ASSERT(0);
		return;
	}

	/*
	 * Using VCO wide-range setting, STM32_RCC_PLLCFG_PLL1VCOSEL_WIDE,
	 * requires input frequency to be between 2MHz and 16MHz.
	 */
	ASSERT(FREQ_2MHZ <= (STM32_HSI_CLOCK/divm));
	ASSERT((STM32_HSI_CLOCK/divm) <= FREQ_16MHZ);

	/*
	 * Ensure that we actually reach the target frequency.
	 */
	ASSERT((STM32_HSI_CLOCK / divm * divn / divp) == output_freq);

	/* Configure PLL1 using 64 Mhz HSI as input */
	STM32_RCC_PLLCKSELR = STM32_RCC_PLLCKSEL_PLLSRC_HSI
			    | STM32_RCC_PLLCKSEL_DIVM1(divm);
	/* in integer mode, wide range VCO with 16Mhz input, use divP */
	STM32_RCC_PLLCFGR = STM32_RCC_PLLCFG_PLL1VCOSEL_WIDE
			  | STM32_RCC_PLLCFG_PLL1RGE_8M_16M
			  | STM32_RCC_PLLCFG_DIVP1EN;
	STM32_RCC_PLL1DIVR = STM32_RCC_PLLDIV_DIVP(divp)
			   | STM32_RCC_PLLDIV_DIVN(divn);
}

/**
 * Configure peripheral domain prescalers to allow a given sysclk frequency.
 *
 * @param sysclk The input system clock, after the system clock prescaler.
 * @return The bus clock speed selected and configured
 */
static enum freq clock_peripheral_configure(enum freq sysclk) {
	switch (sysclk)
	{
	case FREQ_64MHZ:
		/* Restore /1 HPRE (AHB prescaler) */
		/* Disable downstream prescalers */
		STM32_RCC_D1CFGR = STM32_RCC_D1CFGR_HPRE_DIV1
				 | STM32_RCC_D1CFGR_D1PPRE_DIV1
				 | STM32_RCC_D1CFGR_D1CPRE_DIV1;
		/* TODO(b/149512910): Adjust more peripheral prescalers */
		return FREQ_64MHZ;
	case FREQ_400MHZ:
		/* Put /2 on HPRE (AHB prescaler) to keep at the 200MHz max */
		STM32_RCC_D1CFGR = STM32_RCC_D1CFGR_HPRE_DIV2
				 | STM32_RCC_D1CFGR_D1PPRE_DIV1
				 | STM32_RCC_D1CFGR_D1CPRE_DIV1;
		/* TODO(b/149512910): Adjust more peripheral prescalers */
		return FREQ_200MHZ;
	default:
		ASSERT(0);
		return 0;
	}
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
	case OSC_PLL:
		ready = STM32_RCC_CR_PLL1RDY;
		on = STM32_RCC_CR_PLL1ON;
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

static void switch_voltage_scale(enum voltage_scale vos)
{
	volatile uint32_t *const vos_reg   = &STM32_PWR_D3CR;
	const uint32_t           vos_ready = STM32_PWR_D3CR_VOSRDY;
	const uint32_t           vos_mask  = STM32_PWR_D3CR_VOSMASK;
	const uint32_t           vos_values[] = {
						/* See note below about VOS0. */
						STM32_PWR_D3CR_VOS1,
						STM32_PWR_D3CR_VOS1,
						STM32_PWR_D3CR_VOS2,
						STM32_PWR_D3CR_VOS3,
						};
	BUILD_ASSERT(ARRAY_SIZE(vos_values) == VOLTAGE_SCALE_COUNT);

	/*
	 * Real VOS0 on the H743 requires entering VOS1 and setting an extra
	 * SYS boost register. We currently do not implement this functionality.
	 */
	if (vos == VOLTAGE_SCALE0) {
		ASSERT(0);
		return;
	}

	*vos_reg &= ~vos_mask;
	*vos_reg |= vos_values[vos];
	while (!(*vos_reg & vos_ready))
		;
}

static void clock_set_osc(enum clock_osc osc)
{
	enum freq target_sysclk_freq = FREQ_64MHZ;
	enum voltage_scale target_voltage_scale = VOLTAGE_SCALE3;

	if (osc == current_osc)
		return;

	switch (osc) {
	case OSC_HSI:
	case OSC_PLL:
		break;
	default:
		ASSERT(0);
		return;
	}

	hook_notify(HOOK_PRE_FREQ_CHANGE);

	switch (osc) {
	default:
	case OSC_HSI:
		/* Switch to HSI */
		clock_switch_osc(osc);
		current_bus_freq = clock_peripheral_configure(target_sysclk_freq);
		/* Use more optimized flash latency settings for 64-MHz ACLK */
		clock_flash_latency(current_bus_freq, target_voltage_scale);
		/* Turn off the PLL1 to save power */
		clock_enable_osc(OSC_PLL, false);
		switch_voltage_scale(target_voltage_scale);
		break;

	case OSC_PLL:
		/*
		 * PLL1 configuration:
		 * CPU freq = VCO / DIVP = HSI / DIVM * DIVN / DIVP
		 *          = 64 / 4 * 50 / 2
		 *          = 400 Mhz
		 * System clock = 400 Mhz
		 *  HPRE = /2  => AHB/Timer clock = 200 Mhz
		 */
		target_sysclk_freq = FREQ_400MHZ;
		target_voltage_scale = VOLTAGE_SCALE1;

		switch_voltage_scale(target_voltage_scale);
		clock_pll1_configure(target_sysclk_freq);
		/* turn on PLL1 and wait until it's ready */
		clock_enable_osc(OSC_PLL, true);
		current_bus_freq = clock_peripheral_configure(target_sysclk_freq);
		/* Increase flash latency before transition the clock */
		clock_flash_latency(current_bus_freq, target_voltage_scale);

		/* Switch to PLL */
		clock_switch_osc(OSC_PLL);
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
	ccprintf("Time spent in deep-sleep:            %.6llds\n",
			idle_dsleep_time_us);
	ccprintf("Total time on:                       %.6llds\n", ts.val);
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
	clock_flash_latency(FREQ_64MHZ, VOLTAGE_SCALE3);

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
	ccprintf("Clock frequency is now %d Hz\n", clock_get_freq());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(clock, command_clock,
			"hsi | pll", "Set clock frequency");
