/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Register map for the STM32 family of chips
 *
 * This header file should only contain register definitions and
 * functionality that are common to all STM32 chips.
 * Any chip/family specific macros must be placed in their family
 * specific registers file, which is conditionally included at the
 * end of this file.
 * Include this file directly for all STM32 register definitions.
 *
 * ### History and Reasoning ###
 * In a time before chip family register file separation,
 * long long ago, there lived a single file called `registers.h`,
 * which housed register definitions for all STM32 chip family and variants.
 * This poor file was 3000 lines of register macros and C definitions,
 * swiss-cheesed by nested preprocessor conditional logic.
 * Adding a single new chip variant required splitting multiple,
 * already nested, conditional sections throughout the file.
 * Readability was on the difficult side and refactoring was dangerous.
 *
 * The number of STM32 variants had outgrown the single registers file model.
 * The minor gains of sharing a set of registers between a subset of chip
 * variants no longer outweighed the complexity of the following operations:
 * - Adding a new chip variant or variant feature
 * - Determining if a register was properly setup for a variant or if it
 *   was simply not unset
 *
 * To strike a balance between shared registers and chip specific registers,
 * the registers.h file remains a place for common definitions, but family
 * specific definitions were moved to their own files.
 * These family specific files contain a much reduced level of preprocessor
 * logic for variant specific registers.
 *
 * See https://crrev.com/c/1674679 to witness the separation steps.
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"
#include "compile_time_macros.h"

#ifndef __ASSEMBLER__

/* Register definitions */

/* --- USART --- */
#define STM32_USART_BASE(n) CONCAT3(STM32_USART, n, _BASE)
#define STM32_USART_REG(base, offset) REG32((base) + (offset))

#define STM32_IRQ_USART(n) CONCAT2(STM32_IRQ_USART, n)

/* --- TIMERS --- */
#define STM32_TIM_BASE(n) CONCAT3(STM32_TIM, n, _BASE)

#define STM32_TIM_REG(n, offset) REG16(STM32_TIM_BASE(n) + (offset))
#define STM32_TIM_REG32(n, offset) REG32(STM32_TIM_BASE(n) + (offset))

/*
 * TIM_CR1 bits below verified against reference manuals for STM32L5, STM32L4,
 * STM32G4, and STM32F0, and assumed to be valid for all STM32 models.
 */
#define STM32_TIM_CR1(n) STM32_TIM_REG(n, 0x00)
#define STM32_TIM_CR1_CEN BIT(0)
#define STM32_TIM_CR1_UDIS BIT(1)
#define STM32_TIM_CR1_URS BIT(2)
#define STM32_TIM_CR1_OPM BIT(3)
#define STM32_TIM_CR1_DIR_MASK BIT(4)
#define STM32_TIM_CR1_DIR_UP 0
#define STM32_TIM_CR1_DIR_DOWN BIT(4)
#define STM32_TIM_CR1_CMS (((n) & 0x3) << 5)
#define STM32_TIM_CR1_CMS_MASK STM32_TIM_CR1_CMS(~0)
#define STM32_TIM_CR1_CMS_EDGE STM32_TIM_CR1_CMS(0)
#define STM32_TIM_CR1_CMS_CENTER1 STM32_TIM_CR1_CMS(1)
#define STM32_TIM_CR1_CMS_CENTER2 STM32_TIM_CR1_CMS(2)
#define STM32_TIM_CR1_CMS_CENTER3 STM32_TIM_CR1_CMS(3)
#define STM32_TIM_CR1_ARPE BIT(7)
#define STM32_TIM_CR1_CKD (((n) & 0x3) << 8)
#define STM32_TIM_CR1_CKD_MASK STM32_TIM_CR1_CKD(~0)
#define STM32_TIM_CR1_CKD_DIV1 STM32_TIM_CR1_CKD(0)
#define STM32_TIM_CR1_CKD_DIV2 STM32_TIM_CR1_CKD(1)
#define STM32_TIM_CR1_CKD_DIV4 STM32_TIM_CR1_CKD(2)
#define STM32_TIM_CR1_UIFREMAP BIT(11)
#define STM32_TIM_CR2(n) STM32_TIM_REG(n, 0x04)
#define STM32_TIM_SMCR(n) STM32_TIM_REG(n, 0x08)
#define STM32_TIM_DIER(n) STM32_TIM_REG(n, 0x0C)
#define STM32_TIM_SR(n) STM32_TIM_REG(n, 0x10)
#define STM32_TIM_EGR(n) STM32_TIM_REG(n, 0x14)
#define STM32_TIM_EGR_UG BIT(0)
#define STM32_TIM_CCMR1(n) STM32_TIM_REG(n, 0x18)
#define STM32_TIM_CCMR1_OC1PE BIT(2)
/* Use in place of TIM_CCMR1_OC1M_0 through 2 from STM documentation. */
#define STM32_TIM_CCMR1_OC1M(n) (((n) & 0x7) << 4)
#define STM32_TIM_CCMR1_OC1M_MASK STM32_TIM_CCMR1_OC1M(~0)
#define STM32_TIM_CCMR1_OC1M_FROZEN STM32_TIM_CCMR1_OC1M(0x0)
#define STM32_TIM_CCMR1_OC1M_ACTIVE_ON_MATCH STM32_TIM_CCMR1_OC1M(0x1)
#define STM32_TIM_CCMR1_OC1M_INACTIVE_ON_MATCH STM32_TIM_CCMR1_OC1M(0x2)
#define STM32_TIM_CCMR1_OC1M_TOGGLE STM32_TIM_CCMR1_OC1M(0x3)
#define STM32_TIM_CCMR1_OC1M_FORCE_INACTIVE STM32_TIM_CCMR1_OC1M(0x4)
#define STM32_TIM_CCMR1_OC1M_FORCE_ACTIVE STM32_TIM_CCMR1_OC1M(0x5)
#define STM32_TIM_CCMR1_OC1M_PWM_MODE_1 STM32_TIM_CCMR1_OC1M(0x6)
#define STM32_TIM_CCMR1_OC1M_PWM_MODE_2 STM32_TIM_CCMR1_OC1M(0x7)
#define STM32_TIM_CCMR2(n) STM32_TIM_REG(n, 0x1C)
#define STM32_TIM_CCER(n) STM32_TIM_REG(n, 0x20)
#define STM32_TIM_CCER_CC1E BIT(0)
#define STM32_TIM_CCER_CC1P BIT(1)
#define STM32_TIM_CCER_CC1NE BIT(2)
#define STM32_TIM_CCER_CC1NP BIT(3)
#define STM32_TIM_CNT(n) STM32_TIM_REG(n, 0x24)
#define STM32_TIM_PSC(n) STM32_TIM_REG(n, 0x28)
#define STM32_TIM_ARR(n) STM32_TIM_REG(n, 0x2C)
#define STM32_TIM_RCR(n) STM32_TIM_REG(n, 0x30)
#define STM32_TIM_CCR1(n) STM32_TIM_REG(n, 0x34)
#define STM32_TIM_CCR2(n) STM32_TIM_REG(n, 0x38)
#define STM32_TIM_CCR3(n) STM32_TIM_REG(n, 0x3C)
#define STM32_TIM_CCR4(n) STM32_TIM_REG(n, 0x40)
#define STM32_TIM_BDTR(n) STM32_TIM_REG(n, 0x44)
#define STM32_TIM_BDTR_MOE BIT(15)
#define STM32_TIM_DCR(n) STM32_TIM_REG(n, 0x48)
#define STM32_TIM_DMAR(n) STM32_TIM_REG(n, 0x4C)
#define STM32_TIM_OR(n) STM32_TIM_REG(n, 0x50)

#define STM32_TIM_CCRx(n, x) STM32_TIM_REG(n, 0x34 + ((x)-1) * 4)

#define STM32_TIM32_CNT(n) STM32_TIM_REG32(n, 0x24)
#define STM32_TIM32_ARR(n) STM32_TIM_REG32(n, 0x2C)
#define STM32_TIM32_CCR1(n) STM32_TIM_REG32(n, 0x34)
#define STM32_TIM32_CCR2(n) STM32_TIM_REG32(n, 0x38)
#define STM32_TIM32_CCR3(n) STM32_TIM_REG32(n, 0x3C)
#define STM32_TIM32_CCR4(n) STM32_TIM_REG32(n, 0x40)
/* Timer registers as struct */
struct timer_ctlr {
	unsigned int cr1;
	unsigned int cr2;
	unsigned int smcr;
	unsigned int dier;

	unsigned int sr;
	unsigned int egr;
	unsigned int ccmr1;
	unsigned int ccmr2;

	unsigned int ccer;
	unsigned int cnt;
	unsigned int psc;
	unsigned int arr;

	unsigned int ccr[5]; /* ccr[0] = reserved30 */

	unsigned int bdtr;
	unsigned int dcr;
	unsigned int dmar;

	unsigned int option_register;
};
/* Must be volatile, or compiler optimizes out repeated accesses */
typedef volatile struct timer_ctlr timer_ctlr_t;

#define IRQ_TIM(n) CONCAT2(STM32_IRQ_TIM, n)

/* --- Low power timers --- */
#define STM32_LPTIM_BASE(n) CONCAT3(STM32_LPTIM, n, _BASE)

#define STM32_LPTIM_REG(n, offset) REG32(STM32_LPTIM_BASE(n) + (offset))

#define STM32_LPTIM_ISR(n) STM32_LPTIM_REG(n, 0x00)
#define STM32_LPTIM_ICR(n) STM32_LPTIM_REG(n, 0x04)
#define STM32_LPTIM_IER(n) STM32_LPTIM_REG(n, 0x08)
#define STM32_LPTIM_INT_DOWN BIT(6)
#define STM32_LPTIM_INT_UP BIT(5)
#define STM32_LPTIM_INT_ARROK BIT(4)
#define STM32_LPTIM_INT_CMPOK BIT(3)
#define STM32_LPTIM_INT_EXTTRIG BIT(2)
#define STM32_LPTIM_INT_ARRM BIT(1)
#define STM32_LPTIM_INT_CMPM BIT(0)
#define STM32_LPTIM_CFGR(n) STM32_LPTIM_REG(n, 0x0C)
#define STM32_LPTIM_CR(n) STM32_LPTIM_REG(n, 0x10)
#define STM32_LPTIM_CR_RSTARE BIT(4)
#define STM32_LPTIM_CR_COUNTRST BIT(3)
#define STM32_LPTIM_CR_CNTSTRT BIT(2)
#define STM32_LPTIM_CR_SNGSTRT BIT(1)
#define STM32_LPTIM_CR_ENABLE BIT(0)
#define STM32_LPTIM_CMP(n) STM32_LPTIM_REG(n, 0x14)
#define STM32_LPTIM_ARR(n) STM32_LPTIM_REG(n, 0x18)
#define STM32_LPTIM_CNT(n) STM32_LPTIM_REG(n, 0x1C)
#define STM32_LPTIM_CFGR2(n) STM32_LPTIM_REG(n, 0x24)

/* --- GPIO --- */

#define GPIO_A STM32_GPIOA_BASE
#define GPIO_B STM32_GPIOB_BASE
#define GPIO_C STM32_GPIOC_BASE
#define GPIO_D STM32_GPIOD_BASE
#define GPIO_E STM32_GPIOE_BASE
#define GPIO_F STM32_GPIOF_BASE
#define GPIO_G STM32_GPIOG_BASE
#define GPIO_H STM32_GPIOH_BASE
#define GPIO_I STM32_GPIOI_BASE
#define GPIO_J STM32_GPIOJ_BASE
#define GPIO_K STM32_GPIOK_BASE

#define UNIMPLEMENTED_GPIO_BANK GPIO_A

/* --- I2C --- */
#define STM32_I2C1_PORT 0
#define STM32_I2C2_PORT 1
#define STM32_I2C3_PORT 2
#define STM32_FMPI2C4_PORT 3

#if defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32L5)
#define stm32_i2c_reg(port, offset)                                          \
	((uint16_t *)(((port) == 3 ? STM32_I2C4_BASE :                       \
				     (STM32_I2C1_BASE + ((port) * 0x400))) + \
		      (offset)))
#else
#define stm32_i2c_reg(port, offset) \
	((uint16_t *)((STM32_I2C1_BASE + ((port) * 0x400)) + (offset)))
#endif

/* --- Power / Reset / Clocks --- */
#define STM32_PWR_CR REG32(STM32_PWR_BASE + 0x00)
#define STM32_PWR_CR_LPSDSR (1 << 0)
#define STM32_PWR_CR_FLPS (1 << 9)
#define STM32_PWR_CR_SVOS5 (1 << 14)
#define STM32_PWR_CR_SVOS4 (2 << 14)
#define STM32_PWR_CR_SVOS3 (3 << 14)
#define STM32_PWR_CR_SVOS_MASK (3 << 14)

/* RTC domain control register */
#define STM32_RCC_BDCR_BDRST BIT(16)
#define STM32_RCC_BDCR_RTCEN BIT(15)
#define STM32_RCC_BDCR_LSERDY BIT(1)
#define STM32_RCC_BDCR_LSEON BIT(0)
#define BDCR_RTCSEL_MASK ((0x3) << 8)
#define BDCR_RTCSEL(source) (((source) << 8) & BDCR_RTCSEL_MASK)
#define BDCR_SRC_LSE 0x1
#define BDCR_SRC_LSI 0x2
#define BDCR_SRC_HSE 0x3
/* Peripheral bits for RCC_APB/AHB and DBGMCU regs */
#define STM32_RCC_PB1_TIM2 BIT(0)
#define STM32_RCC_PB1_TIM3 BIT(1)
#define STM32_RCC_PB1_TIM4 BIT(2)
#define STM32_RCC_PB1_TIM5 BIT(3)
#define STM32_RCC_PB1_TIM6 BIT(4)
#define STM32_RCC_PB1_TIM7 BIT(5)
#define STM32_RCC_PB1_TIM12 BIT(6) /* STM32H7 */
#define STM32_RCC_PB1_TIM13 BIT(7) /* STM32H7 */
#define STM32_RCC_PB1_TIM14 BIT(8) /* STM32H7 */
#define STM32_RCC_PB1_RTC BIT(10) /* DBGMCU only */
#define STM32_RCC_PB1_WWDG BIT(11)
#define STM32_RCC_PB1_IWDG BIT(12) /* DBGMCU only */
#define STM32_RCC_PB1_SPI2 BIT(14)
#define STM32_RCC_PB1_SPI3 BIT(15)
#define STM32_RCC_PB1_USART2 BIT(17)
#define STM32_RCC_PB1_USART3 BIT(18)
#define STM32_RCC_PB1_USART4 BIT(19)
#define STM32_RCC_PB1_USART5 BIT(20)
#define STM32_RCC_PB1_PWREN BIT(28)
#define STM32_RCC_PB2_SPI1 BIT(12)
/* Reset causes definitions */

/* --- Watchdogs --- */

#define STM32_WWDG_CR REG32(STM32_WWDG_BASE + 0x00)
#define STM32_WWDG_CFR REG32(STM32_WWDG_BASE + 0x04)
#define STM32_WWDG_SR REG32(STM32_WWDG_BASE + 0x08)

#define STM32_WWDG_TB_8 (3 << 7)
#define STM32_WWDG_EWI BIT(9)

#define STM32_IWDG_KR REG32(STM32_IWDG_BASE + 0x00)
#define STM32_IWDG_KR_UNLOCK 0x5555
#define STM32_IWDG_KR_RELOAD 0xaaaa
#define STM32_IWDG_KR_START 0xcccc
#define STM32_IWDG_PR REG32(STM32_IWDG_BASE + 0x04)
#define STM32_IWDG_RLR REG32(STM32_IWDG_BASE + 0x08)
#define STM32_IWDG_RLR_MAX 0x0fff
#define STM32_IWDG_SR REG32(STM32_IWDG_BASE + 0x0C)
#define STM32_IWDG_SR_WVU BIT(2)
#define STM32_IWDG_SR_RVU BIT(1)
#define STM32_IWDG_SR_PVU BIT(0)
#define STM32_IWDG_WINR REG32(STM32_IWDG_BASE + 0x10)

/* --- Real-Time Clock --- */
/* --- Debug --- */
#define STM32_DBGMCU_IDCODE REG32(STM32_DBGMCU_BASE + 0x00)
#define STM32_DBGMCU_CR REG32(STM32_DBGMCU_BASE + 0x04)
/* --- Routing interface --- */
/* STM32L1xx only */
#define STM32_RI_ICR REG32(STM32_COMP_BASE + 0x04)
#define STM32_RI_ASCR1 REG32(STM32_COMP_BASE + 0x08)
#define STM32_RI_ASCR2 REG32(STM32_COMP_BASE + 0x0C)
#define STM32_RI_HYSCR1 REG32(STM32_COMP_BASE + 0x10)
#define STM32_RI_HYSCR2 REG32(STM32_COMP_BASE + 0x14)
#define STM32_RI_HYSCR3 REG32(STM32_COMP_BASE + 0x18)
#define STM32_RI_AMSR1 REG32(STM32_COMP_BASE + 0x1C)
#define STM32_RI_CMR1 REG32(STM32_COMP_BASE + 0x20)
#define STM32_RI_CICR1 REG32(STM32_COMP_BASE + 0x24)
#define STM32_RI_AMSR2 REG32(STM32_COMP_BASE + 0x28)
#define STM32_RI_CMR2 REG32(STM32_COMP_BASE + 0x30)
#define STM32_RI_CICR2 REG32(STM32_COMP_BASE + 0x34)
#define STM32_RI_AMSR3 REG32(STM32_COMP_BASE + 0x38)
#define STM32_RI_CMR3 REG32(STM32_COMP_BASE + 0x3C)
#define STM32_RI_CICR3 REG32(STM32_COMP_BASE + 0x40)
#define STM32_RI_AMSR4 REG32(STM32_COMP_BASE + 0x44)
#define STM32_RI_CMR4 REG32(STM32_COMP_BASE + 0x48)
#define STM32_RI_CICR4 REG32(STM32_COMP_BASE + 0x4C)
#define STM32_RI_AMSR5 REG32(STM32_COMP_BASE + 0x50)
#define STM32_RI_CMR5 REG32(STM32_COMP_BASE + 0x54)
#define STM32_RI_CICR5 REG32(STM32_COMP_BASE + 0x58)

/* --- DAC --- */
#define STM32_DAC_CR REG32(STM32_DAC_BASE + 0x00)
#define STM32_DAC_SWTRIGR REG32(STM32_DAC_BASE + 0x04)
#define STM32_DAC_DHR12R1 REG32(STM32_DAC_BASE + 0x08)
#define STM32_DAC_DHR12L1 REG32(STM32_DAC_BASE + 0x0C)
#define STM32_DAC_DHR8R1 REG32(STM32_DAC_BASE + 0x10)
#define STM32_DAC_DHR12R2 REG32(STM32_DAC_BASE + 0x14)
#define STM32_DAC_DHR12L2 REG32(STM32_DAC_BASE + 0x18)
#define STM32_DAC_DHR8R2 REG32(STM32_DAC_BASE + 0x1C)
#define STM32_DAC_DHR12RD REG32(STM32_DAC_BASE + 0x20)
#define STM32_DAC_DHR12LD REG32(STM32_DAC_BASE + 0x24)
#define STM32_DAC_DHR8RD REG32(STM32_DAC_BASE + 0x28)
#define STM32_DAC_DOR1 REG32(STM32_DAC_BASE + 0x2C)
#define STM32_DAC_DOR2 REG32(STM32_DAC_BASE + 0x30)
#define STM32_DAC_SR REG32(STM32_DAC_BASE + 0x34)

#define STM32_DAC_CR_DMAEN2 BIT(28)
#define STM32_DAC_CR_TSEL2_SWTRG (7 << 19)
#define STM32_DAC_CR_TSEL2_TMR4 (5 << 19)
#define STM32_DAC_CR_TSEL2_TMR2 (4 << 19)
#define STM32_DAC_CR_TSEL2_TMR9 (3 << 19)
#define STM32_DAC_CR_TSEL2_TMR7 (2 << 19)
#define STM32_DAC_CR_TSEL2_TMR6 (0 << 19)
#define STM32_DAC_CR_TSEL2_MASK (7 << 19)
#define STM32_DAC_CR_TEN2 BIT(18)
#define STM32_DAC_CR_BOFF2 BIT(17)
#define STM32_DAC_CR_EN2 BIT(16)
#define STM32_DAC_CR_DMAEN1 BIT(12)
#define STM32_DAC_CR_TSEL1_SWTRG (7 << 3)
#define STM32_DAC_CR_TSEL1_TMR4 (5 << 3)
#define STM32_DAC_CR_TSEL1_TMR2 (4 << 3)
#define STM32_DAC_CR_TSEL1_TMR9 (3 << 3)
#define STM32_DAC_CR_TSEL1_TMR7 (2 << 3)
#define STM32_DAC_CR_TSEL1_TMR6 (0 << 3)
#define STM32_DAC_CR_TSEL1_MASK (7 << 3)
#define STM32_DAC_CR_TEN1 BIT(2)
#define STM32_DAC_CR_BOFF1 BIT(1)
#define STM32_DAC_CR_EN1 BIT(0)
/* --- CRC --- */
#define STM32_CRC_DR REG32(STM32_CRC_BASE + 0x0)
#define STM32_CRC_DR32 REG32(STM32_CRC_BASE + 0x0)
#define STM32_CRC_DR16 REG16(STM32_CRC_BASE + 0x0)
#define STM32_CRC_DR8 REG8(STM32_CRC_BASE + 0x0)

#define STM32_CRC_IDR REG32(STM32_CRC_BASE + 0x4)
#define STM32_CRC_CR REG32(STM32_CRC_BASE + 0x8)
#define STM32_CRC_INIT REG32(STM32_CRC_BASE + 0x10)
#define STM32_CRC_POL REG32(STM32_CRC_BASE + 0x14)

#define STM32_CRC_CR_RESET BIT(0)
#define STM32_CRC_CR_POLYSIZE_32 (0 << 3)
#define STM32_CRC_CR_POLYSIZE_16 (1 << 3)
#define STM32_CRC_CR_POLYSIZE_8 (2 << 3)
#define STM32_CRC_CR_POLYSIZE_7 (3 << 3)
#define STM32_CRC_CR_REV_IN_BYTE (1 << 5)
#define STM32_CRC_CR_REV_IN_HWORD (2 << 5)
#define STM32_CRC_CR_REV_IN_WORD (3 << 5)
#define STM32_CRC_CR_REV_OUT BIT(7)

/* --- PMSE --- */
#define STM32_PMSE_ARCR REG32(STM32_PMSE_BASE + 0x0)
#define STM32_PMSE_ACCR REG32(STM32_PMSE_BASE + 0x4)
#define STM32_PMSE_CR REG32(STM32_PMSE_BASE + 0x8)
#define STM32_PMSE_CRTDR REG32(STM32_PMSE_BASE + 0x14)
#define STM32_PMSE_IER REG32(STM32_PMSE_BASE + 0x18)
#define STM32_PMSE_SR REG32(STM32_PMSE_BASE + 0x1c)
#define STM32_PMSE_IFCR REG32(STM32_PMSE_BASE + 0x20)
#define STM32_PMSE_PxPMR(x) REG32(STM32_PMSE_BASE + 0x2c + (x) * 4)
#define STM32_PMSE_PAPMR REG32(STM32_PMSE_BASE + 0x2c)
#define STM32_PMSE_PBPMR REG32(STM32_PMSE_BASE + 0x30)
#define STM32_PMSE_PCPMR REG32(STM32_PMSE_BASE + 0x34)
#define STM32_PMSE_PDPMR REG32(STM32_PMSE_BASE + 0x38)
#define STM32_PMSE_PEPMR REG32(STM32_PMSE_BASE + 0x3c)
#define STM32_PMSE_PFPMR REG32(STM32_PMSE_BASE + 0x40)
#define STM32_PMSE_PGPMR REG32(STM32_PMSE_BASE + 0x44)
#define STM32_PMSE_PHPMR REG32(STM32_PMSE_BASE + 0x48)
#define STM32_PMSE_PIPMR REG32(STM32_PMSE_BASE + 0x4c)
#define STM32_PMSE_MRCR REG32(STM32_PMSE_BASE + 0x100)
#define STM32_PMSE_MCCR REG32(STM32_PMSE_BASE + 0x104)

/* --- USB --- */
#define STM32_USB_EP(n) REG16(STM32_USB_FS_BASE + (n) * 4)

#define STM32_USB_CNTR REG16(STM32_USB_FS_BASE + 0x40)

#define STM32_USB_CNTR_FRES BIT(0)
#define STM32_USB_CNTR_PDWN BIT(1)
#define STM32_USB_CNTR_LP_MODE BIT(2)
#define STM32_USB_CNTR_FSUSP BIT(3)
#define STM32_USB_CNTR_RESUME BIT(4)
#define STM32_USB_CNTR_L1RESUME BIT(5)
#define STM32_USB_CNTR_L1REQM BIT(7)
#define STM32_USB_CNTR_ESOFM BIT(8)
#define STM32_USB_CNTR_SOFM BIT(9)
#define STM32_USB_CNTR_RESETM BIT(10)
#define STM32_USB_CNTR_SUSPM BIT(11)
#define STM32_USB_CNTR_WKUPM BIT(12)
#define STM32_USB_CNTR_ERRM BIT(13)
#define STM32_USB_CNTR_PMAOVRM BIT(14)
#define STM32_USB_CNTR_CTRM BIT(15)

#define STM32_USB_ISTR REG16(STM32_USB_FS_BASE + 0x44)

#define STM32_USB_ISTR_EP_ID_MASK (0x000f)
#define STM32_USB_ISTR_DIR BIT(4)
#define STM32_USB_ISTR_L1REQ BIT(7)
#define STM32_USB_ISTR_ESOF BIT(8)
#define STM32_USB_ISTR_SOF BIT(9)
#define STM32_USB_ISTR_RESET BIT(10)
#define STM32_USB_ISTR_SUSP BIT(11)
#define STM32_USB_ISTR_WKUP BIT(12)
#define STM32_USB_ISTR_ERR BIT(13)
#define STM32_USB_ISTR_PMAOVR BIT(14)
#define STM32_USB_ISTR_CTR BIT(15)

#define STM32_USB_FNR REG16(STM32_USB_FS_BASE + 0x48)

#define STM32_USB_FNR_RXDP_RXDM_SHIFT (14)
#define STM32_USB_FNR_RXDP_RXDM_MASK (3 << STM32_USB_FNR_RXDP_RXDM_SHIFT)

#define STM32_USB_DADDR REG16(STM32_USB_FS_BASE + 0x4C)
#define STM32_USB_BTABLE REG16(STM32_USB_FS_BASE + 0x50)
#define STM32_USB_LPMCSR REG16(STM32_USB_FS_BASE + 0x54)
#define STM32_USB_BCDR REG16(STM32_USB_FS_BASE + 0x58)

#define STM32_USB_BCDR_BCDEN BIT(0)
#define STM32_USB_BCDR_DCDEN BIT(1)
#define STM32_USB_BCDR_PDEN BIT(2)
#define STM32_USB_BCDR_SDEN BIT(3)
#define STM32_USB_BCDR_DCDET BIT(4)
#define STM32_USB_BCDR_PDET BIT(5)
#define STM32_USB_BCDR_SDET BIT(6)
#define STM32_USB_BCDR_PS2DET BIT(7)

#define EP_MASK 0x0F0F
#define EP_TX_DTOG 0x0040
#define EP_TX_MASK 0x0030
#define EP_TX_VALID 0x0030
#define EP_TX_NAK 0x0020
#define EP_TX_STALL 0x0010
#define EP_TX_DISAB 0x0000
#define EP_RX_DTOG 0x4000
#define EP_RX_MASK 0x3000
#define EP_RX_VALID 0x3000
#define EP_RX_NAK 0x2000
#define EP_RX_STALL 0x1000
#define EP_RX_DISAB 0x0000

#define EP_STATUS_OUT 0x0100

#define EP_TX_RX_MASK (EP_TX_MASK | EP_RX_MASK)
#define EP_TX_RX_VALID (EP_TX_VALID | EP_RX_VALID)
#define EP_TX_RX_NAK (EP_TX_NAK | EP_RX_NAK)

#define STM32_TOGGLE_EP(n, mask, val, flags) \
	STM32_USB_EP(n) =                    \
		(((STM32_USB_EP(n) & (EP_MASK | (mask))) ^ (val)) | (flags))

/* --- TRNG --- */
#define STM32_RNG_CR REG32(STM32_RNG_BASE + 0x0)
#define STM32_RNG_CR_RNGEN BIT(2)
#define STM32_RNG_CR_IE BIT(3)
#define STM32_RNG_CR_CED BIT(5)
#define STM32_RNG_SR REG32(STM32_RNG_BASE + 0x4)
#define STM32_RNG_SR_DRDY BIT(0)
#define STM32_RNG_DR REG32(STM32_RNG_BASE + 0x8)

/* --- AXI interconnect --- */

/* STM32H7: AXI_TARGx_FN_MOD exists for masters x = 1, 2 and 7 */
#define STM32_AXI_TARG_FN_MOD(x) REG32(STM32_GPV_BASE + 0x1108 + 0x1000 * (x))
#define WRITE_ISS_OVERRIDE BIT(1)
#define READ_ISS_OVERRIDE BIT(0)

/* --- MISC --- */
#define STM32_UNIQUE_ID_ADDRESS REG32_ADDR(STM32_UNIQUE_ID_BASE)
#define STM32_UNIQUE_ID_LENGTH (3 * 4)

#endif /* !__ASSEMBLER__ */

#if defined(CHIP_FAMILY_STM32F0)
#include "registers-stm32f0.h"
#elif defined(CHIP_FAMILY_STM32F3)
#include "registers-stm32f3.h"
#elif defined(CHIP_FAMILY_STM32F4)
#include "registers-stm32f4.h"
#elif defined(CHIP_FAMILY_STM32F7)
#include "registers-stm32f7.h"
#elif defined(CHIP_FAMILY_STM32G4)
#include "registers-stm32g4.h"
#elif defined(CHIP_FAMILY_STM32H7)
#include "registers-stm32h7.h"
#elif defined(CHIP_FAMILY_STM32L)
#include "registers-stm32l.h"
#elif defined(CHIP_FAMILY_STM32L4)
#include "registers-stm32l4.h"
#elif defined(CHIP_FAMILY_STM32L5)
#include "registers-stm32l5.h"
#else
#error "Unsupported chip family"
#endif

#endif /* __CROS_EC_REGISTERS_H */
