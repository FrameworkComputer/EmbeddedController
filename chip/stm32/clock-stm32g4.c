/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks configuration routines */

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

#define MHZ(x) ((x)*1000000)
#define WAIT_STATE_FREQ_STEP_HZ MHZ(20)
/* PLL configuration constants */
#define STM32G4_SYSCLK_MAX_HZ MHZ(170)
#define STM32G4_HSI_CLK_HZ MHZ(16)
#define STM32G4_PLL_IN_FREQ_HZ MHZ(4)
#define STM32G4_PLL_R 2
#define STM32G4_AHB_PRE 1
#define STM32G4_APB1_PRE 1
#define STM32G4_APB2_PRE 1

enum rcc_clksrc {
	sysclk_rsvd,
	sysclk_hsi,
	sysclk_hse,
	sysclk_pll,
};

static void stm32g4_config_pll(uint32_t hclk_hz, uint32_t pll_src,
			       uint32_t pll_clk_in_hz)
{
	/*
	 * The pll output frequency (Fhclkc) is determined by:
	 *     Fvco = Fosc_in * (PLL_N / PLL_M)
	 *     Fsysclk = Fvco / PLL_R
	 *     Fhclk = Fsysclk / AHBpre = (Fosc * N) /(M * R * AHBpre)
	 *
	 * PLL_N: 8 <= N <= 127
	 * PLL_M: 1 <= M <= 16
	 * PLL_R: 2, 4, 6, or 8
	 *
	 * PLL_input freq (4 - 16 MHz)
	 * Fvco: 2.66 MHz <= Fvco_in <= 8 MHz
	 *       64 MHz <= Fvco_out <= 344 MHz
	 * Fhclk <= 170 MHz
	 *
	 * PLL config parameters are selected given the following assumptions:
	 *     - PLL input freq = 4 MHz
	 *     - PLL_R divider = 2
	 * With these assumptions the value N can be calculated by:
	 *     N = (Fhclk * M * R * AHBpre) / Fosc
	 *     where M = Fosc / F_pllin
	 *     Replacing M gives:
	 *     N = (Fhclk * R * AHBpre) / Fpll_in
	 */
	uint32_t pll_n;
	uint32_t pll_m;
	uint32_t hclk_freq;

	/* Pll input divider = input freq / desired_input_freq */
	pll_m = pll_clk_in_hz / STM32G4_PLL_IN_FREQ_HZ;
	pll_n = (hclk_hz * STM32G4_PLL_R * STM32G4_AHB_PRE) /
		STM32G4_PLL_IN_FREQ_HZ;

	/* validity checks */
	ASSERT(pll_m && (pll_m <= 16));
	ASSERT((pll_n >= 8) && (pll_n <= 127));

	hclk_freq = pll_clk_in_hz * pll_n /
		    (pll_m * STM32G4_PLL_R * STM32G4_AHB_PRE);
	/* Ensure that there aren't any integer rounding errors */
	ASSERT(hclk_freq == hclk_hz);

	/* Program PLL config register */
	STM32_RCC_PLLCFGR =
		PLLCFGR_PLLP(0) | PLLCFGR_PLLR(STM32G4_PLL_R / 2 - 1) |
		PLLCFGR_PLLR_EN | PLLCFGR_PLLQ(0) | PLLCFGR_PLLQ_EN |
		PLLCFGR_PLLN(pll_n) | PLLCFGR_PLLM(pll_m - 1) | pll_src;

	/* Wait until PLL is locked */
	wait_for_ready(&(STM32_RCC_CR), STM32_RCC_CR_PLLON,
		       STM32_RCC_CR_PLLRDY);

	/*
	 * Program prescalers and set system clock source as PLL
	 * Assuming AHB, APB1, and APB2 prescalers are 1, and no clock output
	 * desired so MCO fields are left at reset value.
	 */
	STM32_RCC_CFGR = STM32_RCC_CFGR_SW_PLL;

	/* Wait until the PLL is the system clock source */
	while ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) !=
	       STM32_RCC_CFGR_SWS_PLL)
		;
}

static void stm32g4_config_low_speed_clock(void)
{
	/* Ensure that LSI is ON */
	wait_for_ready(&(STM32_RCC_CSR), STM32_RCC_CSR_LSION,
		       STM32_RCC_CSR_LSIRDY);

	/* Setup RTC Clock input */
	STM32_RCC_BDCR |= STM32_RCC_BDCR_BDRST;
	STM32_RCC_BDCR = STM32_RCC_BDCR_RTCEN | BDCR_RTCSEL(BDCR_SRC_LSI);
}

static void stm32g4_config_high_speed_clock(uint32_t hclk_hz,
					    enum rcc_clksrc sysclk_src,
					    uint32_t pll_clksrc)
{
	/* TODO(b/161502871): PLL is currently only supported clock source */
	ASSERT(sysclk_src == sysclk_pll);

	/* Ensure that HSI is ON */
	wait_for_ready(&(STM32_RCC_CR), STM32_RCC_CR_HSION,
		       STM32_RCC_CR_HSIRDY);

	if (sysclk_src == sysclk_pll) {
		/*
		 * If PLL_R is the desired clock source, then need to calculate
		 * PLL multilier/diviber parameters. Once the PLL output is
		 * stable, then the PLL must be selected as the clock
		 * source. Note, that if the current clock source selection is
		 * the PLL and sysclk frequency == hclk_hz, there is nothing
		 * that needs to be done here.
		 */
		/* If PLL is the clock source, PLL has already been set up. */
		if ((STM32_RCC_CFGR & STM32_RCC_CFGR_SWS_MASK) ==
		    STM32_RCC_CFGR_SWS_PLL)
			return;
		stm32g4_config_pll(hclk_hz, pll_clksrc, STM32G4_HSI_CLK_HZ);
	}
}

void stm32g4_set_flash_ws(uint32_t freq_hz)
{
	int ws;

	ASSERT(freq_hz <= STM32G4_SYSCLK_MAX_HZ);
	/*
	 * Need to calculate and then set number of wait states (in CPU cycles)
	 * required for access to internal flash. The required values can be
	 * found in Table 9 of RM0440 - STM32G4 technical reference manual. A
	 * table lookup is not required though as WS = HCLK (MHz) / 20
	 */
	ws = freq_hz / WAIT_STATE_FREQ_STEP_HZ;
	/* Enable data and instruction cache */
	STM32_FLASH_ACR |= STM32_FLASH_ACR_DCEN | STM32_FLASH_ACR_ICEN |
			   STM32_FLASH_ACR_PRFTEN | ws;
}

void clock_init(void)
{
	/*
	 * The STM32G4 has 3 potential sysclk sources:
	 *   1. HSE -> external cyrstal oscillator circuit
	 *   2. HSI -> Internal RC oscillator (16 MHz output)
	 *   3. PLL -> input from either HSI or HSI
	 *
	 * SYSCLK is routed to AHB via the AHB prescaler. The AHB clock is fed
	 * directly to AHB bus, core, memory, DMA, and cortex FCLK. The AHB bus
	 * clock is then fed to both APB1 and APB2 buses via the APB1 and APB2
	 * prescalers.
	 *
	 * CrosEC doesn't support having multiple clocks of different
	 * frequencies and therefore f(AHB) = f(APB1) = f(APB2) must be
	 * enforced. The max frequency of all these clocks is 170 MHz. Max input
	 * frequency to the PLL is 48 MHz. The M divider can be used to lower
	 * the PLL input frequency if necessary. The PLL has 3 different output
	 * clocks, PLL_P, PLL_Q, and PLL_R. PLL_R is the clock which can be used
	 * as SYSCLK.
	 *
	 * The STM32G4 has an additional 48 MHz internal oscillator that is fed
	 * directly to the USB and RNG blocks.
	 *
	 * The STM32G4 also has a low speed clock which feeds the RTC and IWDG
	 * blocks and as a low power clock source that can be kept running
	 * during stop and standby modes. The low speed clock is generated from:
	 *   1. LSE -> external crystal oscillator (max = 1 MHz)
	 *   2. LSI -> internal fixed 32 kHz
	 *
	 * The initial state following system reset:
	 *  SYSCLK from HSI, AHB, APB1, and APB2 presecaler = 1
	 *  PLL unlocked, RTC enabled on LSE
	 */

	/* Configure flash wait state and enable I/D cache */
	stm32g4_set_flash_ws(CPU_CLOCK);
	/* Set up high speed clock and enable PLL */
	stm32g4_config_high_speed_clock(CPU_CLOCK, sysclk_pll,
					PLLCFGR_PLLSRC_HSI);
	/* Set up low speed clock */
	stm32g4_config_low_speed_clock();
}

int clock_get_timer_freq(void)
{
	/*
	 * STM32G4 timer clocks (TCLK) are either at the same frequency as
	 * PCLK_N when the APB prescaler is 1, and TLCK = 2 * PCLK if
	 * APBn_pre > 1. It's expected that PCLK1 == PCLK2, so only have to
	 * check either of the apb prescalar settings.
	 */
	return (STM32G4_APB1_PRE > 1 ? CPU_CLOCK * 2 : CPU_CLOCK);
}

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
	if (module == MODULE_USB) {
		if (enable) {
			STM32_RCC_APB1ENR |= STM32_RCC_PB1_USB;
			STM32_RCC_CRRCR |= RCC_CRRCR_HSI48O;
		} else {
			STM32_RCC_CRRCR &= ~RCC_CRRCR_HSI48O;
			STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_USB;
		}
	} else if (module == MODULE_I2C) {
		if (enable) {
			/* Enable clocks to I2C modules if necessary */
			STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_I2C1EN |
					      STM32_RCC_APB1ENR1_I2C2EN |
					      STM32_RCC_APB1ENR1_I2C3EN;
			STM32_RCC_APB1ENR2 |= STM32_RCC_APB1ENR2_I2C4EN;
		} else {
			STM32_RCC_APB1ENR1 &= ~(STM32_RCC_APB1ENR1_I2C1EN |
						STM32_RCC_APB1ENR1_I2C2EN |
						STM32_RCC_APB1ENR1_I2C3EN);
			STM32_RCC_APB1ENR2 &= ~STM32_RCC_APB1ENR2_I2C4EN;
		}
	} else if (module == MODULE_ADC) {
		/* TODO does clock select need to be set here too? */
		if (enable)
			STM32_RCC_AHB2ENR |= (STM32_RCC_AHB2ENR_ADC12EN |
					      STM32_RCC_APB2ENR_ADC345EN);
		else
			STM32_RCC_AHB2ENR &= ~(STM32_RCC_AHB2ENR_ADC12EN |
					       STM32_RCC_APB2ENR_ADC345EN);
	} else {
		CPRINTS("stm32g4: enable clock module %d not supported",
			module);
	}
}

int clock_is_module_enabled(enum module_id module)
{
	if (module == MODULE_USB)
		return !!(STM32_RCC_APB1ENR & STM32_RCC_PB1_USB);
	else if (module == MODULE_I2C)
		return !!(STM32_RCC_APB1ENR1 & STM32_RCC_APB1ENR1_I2C1EN);
	else if (module == MODULE_ADC)
		return !!(STM32_RCC_AHB2ENR & STM32_RCC_AHB2ENR_ADC12EN);
	return 0;
}
