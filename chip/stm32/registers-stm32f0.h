/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Register map for the STM32F0 family of chips
 *
 * This header file should not be included directly.
 * Please include registers.h instead.
 *
 * Known Chip Variants
 * - STM32F03X
 * - STM32F05X
 * - STM32F070
 * - STM32F07X
 * - STM32F09X
 */

#ifndef __CROS_EC_REGISTERS_H
#error "This header file should not be included directly."
#endif

/* --- IRQ numbers --- */
#define STM32_IRQ_WWDG 0
#define STM32_IRQ_PVD 1
#define STM32_IRQ_RTC_WAKEUP 2
#define STM32_IRQ_RTC_ALARM 2
#define STM32_IRQ_FLASH 3
#define STM32_IRQ_RCC 4
#define STM32_IRQ_EXTI0_1 5
#define STM32_IRQ_EXTI2_3 6
#define STM32_IRQ_EXTI4_15 7
#define STM32_IRQ_TSC 8
#define STM32_IRQ_DMA_CHANNEL_1 9
#define STM32_IRQ_DMA_CHANNEL_2_3 10
#define STM32_IRQ_DMA_CHANNEL_4_7 11
#define STM32_IRQ_ADC_COMP 12
#define STM32_IRQ_TIM1_BRK_UP_TRG 13
#define STM32_IRQ_TIM1_CC 14
#define STM32_IRQ_TIM2 15
#define STM32_IRQ_TIM3 16
#define STM32_IRQ_TIM6_DAC 17
#define STM32_IRQ_TIM7 18
#define STM32_IRQ_TIM14 19
#define STM32_IRQ_TIM15 20
#define STM32_IRQ_TIM16 21
#define STM32_IRQ_TIM17 22
#define STM32_IRQ_I2C1 23
#define STM32_IRQ_I2C2 24
#define STM32_IRQ_SPI1 25
#define STM32_IRQ_SPI2 26
#define STM32_IRQ_USART1 27
#define STM32_IRQ_USART2 28
#define STM32_IRQ_USART3_4 29
#define STM32_IRQ_CEC_CAN 30
#define STM32_IRQ_USB 31
/* aliases for easier code sharing */
#define STM32_IRQ_COMP STM32_IRQ_ADC_COMP
#define STM32_IRQ_USB_LP STM32_IRQ_USB

/* Peripheral base addresses */
#define STM32_ADC1_BASE 0x40012400
#define STM32_ADC_BASE 0x40012700 /* STM32L15X only */

#define STM32_CEC_BASE 0x40007800 /* STM32F373 */
#define STM32_CRC_BASE 0x40023000
#define STM32_CRS_BASE 0x40006c00 /* STM32F0XX */
#define STM32_DAC_BASE 0x40007400

#define STM32_COMP_BASE 0x40010000

#define STM32_DBGMCU_BASE 0x40015800

#define STM32_DMA1_BASE 0x40020000
#define STM32_DMA2_BASE 0x40020400

#define STM32_EXTI_BASE 0x40010400

#define STM32_FLASH_REGS_BASE 0x40022000

#define STM32_GPIOA_BASE 0x48000000
#define STM32_GPIOB_BASE 0x48000400
#define STM32_GPIOC_BASE 0x48000800
#define STM32_GPIOD_BASE 0x48000C00
#define STM32_GPIOE_BASE 0x48001000
#define STM32_GPIOF_BASE 0x48001400
#define STM32_GPIOG_BASE 0x48001800 /* only for stm32l4x6 */
#define STM32_GPIOH_BASE 0x48001C00 /* only for stm32l4 */

#define STM32_I2C1_BASE 0x40005400
#define STM32_I2C2_BASE 0x40005800
#define STM32_I2C3_BASE 0x40005C00
#define STM32_I2C4_BASE 0x40006000

#define STM32_IWDG_BASE 0x40003000
#define STM32_LCD_BASE 0x40002400

#define STM32_OPTB_BASE 0x1FFFF800

#define STM32_PMSE_BASE 0x40013400
#define STM32_PWR_BASE 0x40007000

#define STM32_RCC_BASE 0x40021000

#define STM32_RI_BASE 0x40007C00 /* STM32L1xx only */
#define STM32_RNG_BASE 0x50060800 /* STM32L4 */
#define STM32_RTC_BASE 0x40002800

#define STM32_SPI1_BASE 0x40013000
#define STM32_SPI2_BASE 0x40003800
#define STM32_SPI3_BASE 0x40003c00 /* STM32F373, STM32L4, STM32F7 */

#define STM32_SYSCFG_BASE 0x40010000

#define STM32_TIM1_BASE 0x40012c00 /* STM32F373 */
#define STM32_TIM2_BASE 0x40000000
#define STM32_TIM3_BASE 0x40000400
#define STM32_TIM4_BASE 0x40000800
#define STM32_TIM5_BASE 0x40000c00 /* STM32F373 */
#define STM32_TIM6_BASE 0x40001000
#define STM32_TIM7_BASE 0x40001400
#define STM32_TIM12_BASE 0x40001800 /* STM32F373 */
#define STM32_TIM13_BASE 0x40001c00 /* STM32F373 */
#define STM32_TIM14_BASE 0x40002000 /* STM32F373 */
#define STM32_TIM15_BASE 0x40014000
#define STM32_TIM16_BASE 0x40014400
#define STM32_TIM17_BASE 0x40014800
#define STM32_TIM18_BASE 0x40009c00 /* STM32F373 only */
#define STM32_TIM19_BASE 0x40015c00 /* STM32F373 only */

#define STM32_UNIQUE_ID_BASE 0x1ffff7ac

#define STM32_USART1_BASE 0x40013800
#define STM32_USART2_BASE 0x40004400
#define STM32_USART3_BASE 0x40004800
#define STM32_USART4_BASE 0x40004c00
#define STM32_USART9_BASE 0x40008000 /* LPUART */

#define STM32_USB_CAN_SRAM_BASE 0x40006000
#define STM32_USB_FS_BASE 0x40005C00

#define STM32_WWDG_BASE 0x40002C00

#ifndef __ASSEMBLER__

/* Register definitions */

/* --- USART --- */
#define STM32_USART_CR1(base) STM32_USART_REG(base, 0x00)
#define STM32_USART_CR1_UE BIT(0)
#define STM32_USART_CR1_UESM BIT(1)
#define STM32_USART_CR1_RE BIT(2)
#define STM32_USART_CR1_TE BIT(3)
#define STM32_USART_CR1_RXNEIE BIT(5)
#define STM32_USART_CR1_TCIE BIT(6)
#define STM32_USART_CR1_TXEIE BIT(7)
#define STM32_USART_CR1_PS BIT(9)
#define STM32_USART_CR1_PCE BIT(10)
#define STM32_USART_CR1_M BIT(12)
#define STM32_USART_CR1_OVER8 BIT(15)
#define STM32_USART_CR2(base) STM32_USART_REG(base, 0x04)
#define STM32_USART_CR2_SWAP BIT(15)
#define STM32_USART_CR3(base) STM32_USART_REG(base, 0x08)
#define STM32_USART_CR3_EIE BIT(0)
#define STM32_USART_CR3_DMAR BIT(6)
#define STM32_USART_CR3_DMAT BIT(7)
#define STM32_USART_CR3_ONEBIT BIT(11)
#define STM32_USART_CR3_OVRDIS BIT(12)
#define STM32_USART_CR3_WUS_START_BIT (2 << 20)
#define STM32_USART_CR3_WUFIE BIT(22)
#define STM32_USART_BRR(base) STM32_USART_REG(base, 0x0C)
#define STM32_USART_GTPR(base) STM32_USART_REG(base, 0x10)
#define STM32_USART_RTOR(base) STM32_USART_REG(base, 0x14)
#define STM32_USART_RQR(base) STM32_USART_REG(base, 0x18)
#define STM32_USART_ISR(base) STM32_USART_REG(base, 0x1C)
#define STM32_USART_ICR(base) STM32_USART_REG(base, 0x20)
#define STM32_USART_ICR_ORECF BIT(3)
#define STM32_USART_ICR_TCCF BIT(6)
#define STM32_USART_RDR(base) STM32_USART_REG(base, 0x24)
#define STM32_USART_TDR(base) STM32_USART_REG(base, 0x28)
#define STM32_USART_PRESC(base) STM32_USART_REG(base, 0x2C)
/* register alias */
#define STM32_USART_SR(base) STM32_USART_ISR(base)
#define STM32_USART_SR_ORE BIT(3)
#define STM32_USART_SR_RXNE BIT(5)
#define STM32_USART_SR_TC BIT(6)
#define STM32_USART_SR_TXE BIT(7)

/* --- GPIO --- */

#define STM32_GPIO_MODER(b) REG32((b) + 0x00)
#define STM32_GPIO_OTYPER(b) REG16((b) + 0x04)
#define STM32_GPIO_OSPEEDR(b) REG32((b) + 0x08)
#define STM32_GPIO_PUPDR(b) REG32((b) + 0x0C)
#define STM32_GPIO_IDR(b) REG16((b) + 0x10)
#define STM32_GPIO_ODR(b) REG16((b) + 0x14)
#define STM32_GPIO_BSRR(b) REG32((b) + 0x18)
#define STM32_GPIO_LCKR(b) REG32((b) + 0x1C)
#define STM32_GPIO_AFRL(b) REG32((b) + 0x20)
#define STM32_GPIO_AFRH(b) REG32((b) + 0x24)
#define STM32_GPIO_BRR(b) REG32((b) + 0x28)
#define STM32_GPIO_ASCR(b) REG32((b) + 0x2C) /* only for stm32l4 */

#define GPIO_ALT_F0 0x0
#define GPIO_ALT_F1 0x1
#define GPIO_ALT_F2 0x2
#define GPIO_ALT_F3 0x3
#define GPIO_ALT_F4 0x4
#define GPIO_ALT_F5 0x5
#define GPIO_ALT_F6 0x6
#define GPIO_ALT_F7 0x7
#define GPIO_ALT_F8 0x8
#define GPIO_ALT_F9 0x9
#define GPIO_ALT_FA 0xA
#define GPIO_ALT_FB 0xB
#define GPIO_ALT_FC 0xC
#define GPIO_ALT_FD 0xD
#define GPIO_ALT_FE 0xE
#define GPIO_ALT_FF 0xF

/* --- I2C --- */
#define STM32_I2C_CR1(n) REG32(stm32_i2c_reg(n, 0x00))
#define STM32_I2C_CR1_PE BIT(0)
#define STM32_I2C_CR1_TXIE BIT(1)
#define STM32_I2C_CR1_RXIE BIT(2)
#define STM32_I2C_CR1_ADDRIE BIT(3)
#define STM32_I2C_CR1_NACKIE BIT(4)
#define STM32_I2C_CR1_STOPIE BIT(5)
#define STM32_I2C_CR1_ERRIE BIT(7)
#define STM32_I2C_CR1_WUPEN BIT(18)
#define STM32_I2C_CR2(n) REG32(stm32_i2c_reg(n, 0x04))
#define STM32_I2C_CR2_RD_WRN BIT(10)
#define STM32_I2C_CR2_START BIT(13)
#define STM32_I2C_CR2_STOP BIT(14)
#define STM32_I2C_CR2_NACK BIT(15)
#define STM32_I2C_CR2_RELOAD BIT(24)
#define STM32_I2C_CR2_AUTOEND BIT(25)
#define STM32_I2C_OAR1(n) REG32(stm32_i2c_reg(n, 0x08))
#define STM32_I2C_OAR2(n) REG32(stm32_i2c_reg(n, 0x0C))
#define STM32_I2C_TIMINGR(n) REG32(stm32_i2c_reg(n, 0x10))
#define STM32_I2C_TIMEOUTR(n) REG32(stm32_i2c_reg(n, 0x14))
#define STM32_I2C_ISR(n) REG32(stm32_i2c_reg(n, 0x18))
#define STM32_I2C_ISR_TXE BIT(0)
#define STM32_I2C_ISR_TXIS BIT(1)
#define STM32_I2C_ISR_RXNE BIT(2)
#define STM32_I2C_ISR_ADDR BIT(3)
#define STM32_I2C_ISR_NACK BIT(4)
#define STM32_I2C_ISR_STOP BIT(5)
#define STM32_I2C_ISR_TC BIT(6)
#define STM32_I2C_ISR_TCR BIT(7)
#define STM32_I2C_ISR_BERR BIT(8)
#define STM32_I2C_ISR_ARLO BIT(9)
#define STM32_I2C_ISR_OVR BIT(10)
#define STM32_I2C_ISR_PECERR BIT(11)
#define STM32_I2C_ISR_TIMEOUT BIT(12)
#define STM32_I2C_ISR_ALERT BIT(13)
#define STM32_I2C_ISR_BUSY BIT(15)
#define STM32_I2C_ISR_DIR BIT(16)
#define STM32_I2C_ISR_ADDCODE(isr) (((isr) >> 16) & 0xfe)
#define STM32_I2C_ICR(n) REG32(stm32_i2c_reg(n, 0x1C))
#define STM32_I2C_ICR_ADDRCF BIT(3)
#define STM32_I2C_ICR_NACKCF BIT(4)
#define STM32_I2C_ICR_STOPCF BIT(5)
#define STM32_I2C_ICR_BERRCF BIT(8)
#define STM32_I2C_ICR_ARLOCF BIT(9)
#define STM32_I2C_ICR_OVRCF BIT(10)
#define STM32_I2C_ICR_TIMEOUTCF BIT(12)
#define STM32_I2C_ICR_ALL 0x3F38
#define STM32_I2C_PECR(n) REG32(stm32_i2c_reg(n, 0x20))
#define STM32_I2C_RXDR(n) REG32(stm32_i2c_reg(n, 0x24))
#define STM32_I2C_TXDR(n) REG32(stm32_i2c_reg(n, 0x28))

/* --- Power / Reset / Clocks --- */
#define STM32_PWR_CR REG32(STM32_PWR_BASE + 0x00)
#define STM32_PWD_PVD_LS_MASK (0x07 << 5)
#define STM32_PWD_PVD_LS(n) ((n & 0x07) << 5)
#define STM32_PWR_PVDE BIT(4)

#define STM32_PWR_CSR REG32(STM32_PWR_BASE + 0x04)

#define STM32_PWR_CSR_EWUP1 BIT(8)
#define STM32_PWR_CSR_EWUP2 BIT(9)
#define STM32_PWR_CSR_EWUP3 BIT(10)
#define STM32_PWR_CSR_EWUP4 BIT(11) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP5 BIT(12) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP6 BIT(13) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP7 BIT(14) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP8 BIT(15) /* STM32F0xx only */

#define STM32_CRS_CR REG32(STM32_CRS_BASE + 0x00) /* STM32F0XX */
#define STM32_CRS_CR_SYNCOKIE BIT(0)
#define STM32_CRS_CR_SYNCWARNIE BIT(1)
#define STM32_CRS_CR_ERRIE BIT(2)
#define STM32_CRS_CR_ESYNCIE BIT(3)
#define STM32_CRS_CR_CEN BIT(5)
#define STM32_CRS_CR_AUTOTRIMEN BIT(6)
#define STM32_CRS_CR_SWSYNC BIT(7)
#define STM32_CRS_CR_TRIM(n) (((n) & 0x3f) << 8)

#define STM32_CRS_CFGR REG32(STM32_CRS_BASE + 0x04) /* STM32F0XX */
#define STM32_CRS_CFGR_RELOAD(n) (((n) & 0xffff) << 0)
#define STM32_CRS_CFGR_FELIM(n) (((n) & 0xff) << 16)
#define STM32_CRS_CFGR_SYNCDIV(n) (((n) & 7) << 24)
#define STM32_CRS_CFGR_SYNCSRC(n) (((n) & 3) << 28)
#define STM32_CRS_CFGR_SYNCPOL BIT(31)

#define STM32_CRS_ISR REG32(STM32_CRS_BASE + 0x08) /* STM32F0XX */
#define STM32_CRS_ISR_SYNCOKF BIT(0)
#define STM32_CRS_ISR_SYNCWARNF BIT(1)
#define STM32_CRS_ISR_ERRF BIT(2)
#define STM32_CRS_ISR_ESYNCF BIT(3)
#define STM32_CRS_ISR_SYNCERR BIT(8)
#define STM32_CRS_ISR_SYNCMISS BIT(9)
#define STM32_CRS_ISR_TRIMOVF BIT(10)
#define STM32_CRS_ISR_FEDIR BIT(15)
#define STM32_CRS_ISR_FECAP (0xffff << 16)

#define STM32_CRS_ICR REG32(STM32_CRS_BASE + 0x0c) /* STM32F0XX */
#define STM32_CRS_ICR_SYNCOKC BIT(0)
#define STM32_CRS_ICR_SYNCWARINC BIT(1)
#define STM32_CRS_ICR_ERRC BIT(2)
#define STM32_CRS_ICR_ESYNCC BIT(3)

#define STM32_RCC_CR REG32(STM32_RCC_BASE + 0x00)
#define STM32_RCC_CFGR REG32(STM32_RCC_BASE + 0x04)
#define STM32_RCC_CIR REG32(STM32_RCC_BASE + 0x08)
#define STM32_RCC_APB2RSTR REG32(STM32_RCC_BASE + 0x0c)
#define STM32_RCC_APB1RSTR REG32(STM32_RCC_BASE + 0x10)
#define STM32_RCC_AHBENR REG32(STM32_RCC_BASE + 0x14)
#define STM32_RCC_APB2ENR REG32(STM32_RCC_BASE + 0x18)
#define STM32_RCC_APB2ENR_ADCEN BIT(9) /* STM32F3/F0 */
#define STM32_RCC_APB2ENR_TIM16EN BIT(17)
#define STM32_RCC_APB2ENR_TIM17EN BIT(18)
#define STM32_RCC_DBGMCUEN BIT(22)
#define STM32_RCC_SYSCFGEN BIT(0)

#define STM32_RCC_APB1ENR REG32(STM32_RCC_BASE + 0x1c)
#define STM32_RCC_DACEN BIT(29)
#define STM32_RCC_PWREN BIT(28)

#define STM32_RCC_BDCR REG32(STM32_RCC_BASE + 0x20)
#define STM32_RCC_CSR REG32(STM32_RCC_BASE + 0x24)
/* STM32F373 */
#define STM32_RCC_CFGR2 REG32(STM32_RCC_BASE + 0x2c)
/* STM32F0XX and STM32F373 */
#define STM32_RCC_CFGR3 REG32(STM32_RCC_BASE + 0x30)
#define STM32_RCC_CR2 REG32(STM32_RCC_BASE + 0x34) /* STM32F0XX */

#define STM32_RCC_HB_DMA1 BIT(0)
/* STM32F373 */
#define STM32_RCC_HB_DMA2 BIT(1)
#define STM32_RCC_PB2_TIM1 BIT(11) /* Except STM32F373 */
#define STM32_RCC_PB2_TIM15 BIT(16) /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB2_TIM16 BIT(17) /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB2_TIM17 BIT(18) /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB2_TIM19 BIT(19) /* STM32F373 */
#define STM32_RCC_PB2_PMAD BIT(11) /* STM32TS */
#define STM32_RCC_PB2_PMSE BIT(13) /* STM32TS */
#define STM32_RCC_PB1_TIM12 BIT(6) /* STM32F373 */
#define STM32_RCC_PB1_TIM13 BIT(7) /* STM32F373 */
#define STM32_RCC_PB1_TIM14 BIT(8) /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB1_TIM18 BIT(9) /* STM32F373 */
#define STM32_RCC_PB1_USB BIT(23)
#define STM32_RCC_PB1_CRS BIT(27)

#define STM32_SYSCFG_CFGR1 REG32(STM32_SYSCFG_BASE + 0x00)
#define STM32_SYSCFG_EXTICR(n) REG32(STM32_SYSCFG_BASE + 8 + 4 * (n))
#define STM32_SYSCFG_CFGR2 REG32(STM32_SYSCFG_BASE + 0x18)

/* Peripheral bits for RCC_APB/AHB and DBGMCU regs */
#define STM32_RCC_PB2_USART1 BIT(14)

/* Reset causes definitions */
/* Reset causes in RCC CSR register */
#define STM32_RCC_RESET_CAUSE STM32_RCC_CSR
#define RESET_CAUSE_WDG 0x60000000
#define RESET_CAUSE_SFT 0x10000000
#define RESET_CAUSE_POR 0x08000000
#define RESET_CAUSE_PIN 0x04000000
#define RESET_CAUSE_OTHER 0xfe000000
#define RESET_CAUSE_RMVF 0x01000000
/* Power cause in PWR CSR register */
#define STM32_PWR_RESET_CAUSE STM32_PWR_CSR
#define STM32_PWR_RESET_CAUSE_CLR STM32_PWR_CR
#define RESET_CAUSE_SBF 0x00000002
#define RESET_CAUSE_SBF_CLR 0x00000004

/* --- Watchdogs --- */

/* --- Real-Time Clock --- */
#define STM32_RTC_TR REG32(STM32_RTC_BASE + 0x00)
#define STM32_RTC_DR REG32(STM32_RTC_BASE + 0x04)
#define STM32_RTC_CR REG32(STM32_RTC_BASE + 0x08)
#define STM32_RTC_CR_BYPSHAD BIT(5)
#define STM32_RTC_CR_ALRAE BIT(8)
#define STM32_RTC_CR_ALRAIE BIT(12)
#define STM32_RTC_ISR REG32(STM32_RTC_BASE + 0x0C)
#define STM32_RTC_ISR_ALRAWF BIT(0)
#define STM32_RTC_ISR_RSF BIT(5)
#define STM32_RTC_ISR_INITF BIT(6)
#define STM32_RTC_ISR_INIT BIT(7)
#define STM32_RTC_ISR_ALRAF BIT(8)
#define STM32_RTC_PRER REG32(STM32_RTC_BASE + 0x10)
#define STM32_RTC_PRER_A_MASK (0x7f << 16)
#define STM32_RTC_PRER_S_MASK (0x7fff << 0)
#define STM32_RTC_WUTR REG32(STM32_RTC_BASE + 0x14)
#define STM32_RTC_CALIBR REG32(STM32_RTC_BASE + 0x18)
#define STM32_RTC_ALRMAR REG32(STM32_RTC_BASE + 0x1C)
#define STM32_RTC_ALRMBR REG32(STM32_RTC_BASE + 0x20)
#define STM32_RTC_WPR REG32(STM32_RTC_BASE + 0x24)
#define STM32_RTC_SSR REG32(STM32_RTC_BASE + 0x28)
#define STM32_RTC_TSTR REG32(STM32_RTC_BASE + 0x30)
#define STM32_RTC_TSDR REG32(STM32_RTC_BASE + 0x34)
#define STM32_RTC_TAFCR REG32(STM32_RTC_BASE + 0x40)
#define STM32_RTC_ALRMASSR REG32(STM32_RTC_BASE + 0x44)
#define STM32_RTC_BACKUP(n) REG32(STM32_RTC_BASE + 0x50 + 4 * (n))

#define STM32_BKP_DATA(n) STM32_RTC_BACKUP(n)
#define STM32_BKP_BYTES 20

/* --- SPI --- */

/* The SPI controller registers */
struct stm32_spi_regs {
	uint16_t cr1;
	uint16_t _pad0;
	uint16_t cr2;
	uint16_t _pad1;
	unsigned int sr;
	uint8_t dr;
	uint8_t _pad2;
	uint16_t _pad3;
	unsigned int crcpr;
	unsigned int rxcrcr;
	unsigned int txcrcr;
	unsigned int i2scfgr; /* STM32L only */
	unsigned int i2spr; /* STM32L only */
};
/* Must be volatile, or compiler optimizes out repeated accesses */
typedef volatile struct stm32_spi_regs stm32_spi_regs_t;

#define STM32_SPI1_REGS ((stm32_spi_regs_t *)STM32_SPI1_BASE)
#define STM32_SPI2_REGS ((stm32_spi_regs_t *)STM32_SPI2_BASE)
#define STM32_SPI3_REGS ((stm32_spi_regs_t *)STM32_SPI3_BASE)
#define STM32_SPI4_REGS ((stm32_spi_regs_t *)STM32_SPI4_BASE)

#define STM32_SPI_CR1_BIDIMODE BIT(15)
#define STM32_SPI_CR1_BIDIOE BIT(14)
#define STM32_SPI_CR1_CRCEN BIT(13)
#define STM32_SPI_CR1_SSM BIT(9)
#define STM32_SPI_CR1_SSI BIT(8)
#define STM32_SPI_CR1_LSBFIRST BIT(7)
#define STM32_SPI_CR1_SPE BIT(6)
#define STM32_SPI_CR1_BR_DIV64R (5 << 3)
#define STM32_SPI_CR1_BR_DIV4R BIT(3)
#define STM32_SPI_CR1_MSTR BIT(2)
#define STM32_SPI_CR1_CPOL BIT(1)
#define STM32_SPI_CR1_CPHA BIT(0)
#define STM32_SPI_CR2_FRXTH BIT(12)
#define STM32_SPI_CR2_DATASIZE(n) (((n)-1) << 8)
#define STM32_SPI_CR2_TXEIE BIT(7)
#define STM32_SPI_CR2_RXNEIE BIT(6)
#define STM32_SPI_CR2_NSSP BIT(3)
#define STM32_SPI_CR2_SSOE BIT(2)
#define STM32_SPI_CR2_TXDMAEN BIT(1)
#define STM32_SPI_CR2_RXDMAEN BIT(0)

#define STM32_SPI_SR_RXNE BIT(0)
#define STM32_SPI_SR_TXE BIT(1)
#define STM32_SPI_SR_CRCERR BIT(4)
#define STM32_SPI_SR_BSY BIT(7)
#define STM32_SPI_SR_FRLVL (3 << 9)
#define STM32_SPI_SR_FTLVL (3 << 11)
/* --- Debug --- */
#define STM32_DBGMCU_APB1FZ REG32(STM32_DBGMCU_BASE + 0x08)
#define STM32_DBGMCU_APB2FZ REG32(STM32_DBGMCU_BASE + 0x0C)

/* --- Flash --- */
#define STM32_FLASH_ACR REG32(STM32_FLASH_REGS_BASE + 0x00)
#define STM32_FLASH_ACR_LATENCY_SHIFT (0)
#define STM32_FLASH_ACR_LATENCY_MASK (7 << STM32_FLASH_ACR_LATENCY_SHIFT)
#define STM32_FLASH_ACR_LATENCY BIT(0)
#define STM32_FLASH_ACR_PRFTEN BIT(4)
#define STM32_FLASH_KEYR REG32(STM32_FLASH_REGS_BASE + 0x04)
#define FLASH_KEYR_KEY1 0x45670123
#define FLASH_KEYR_KEY2 0xCDEF89AB

#define STM32_FLASH_OPTKEYR REG32(STM32_FLASH_REGS_BASE + 0x08)
#define FLASH_OPTKEYR_KEY1 FLASH_KEYR_KEY1
#define FLASH_OPTKEYR_KEY2 FLASH_KEYR_KEY2
#define STM32_FLASH_SR REG32(STM32_FLASH_REGS_BASE + 0x0c)
#define FLASH_SR_BUSY BIT(0)
#define FLASH_SR_PGERR BIT(2)
#define FLASH_SR_WRPRTERR BIT(4)
#define FLASH_SR_ALL_ERR (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)
#define FLASH_SR_EOP BIT(5)
#define STM32_FLASH_CR REG32(STM32_FLASH_REGS_BASE + 0x10)
#define FLASH_CR_PG BIT(0)
#define FLASH_CR_PER BIT(1)
#define FLASH_CR_OPTPG BIT(4)
#define FLASH_CR_OPTER BIT(5)
#define FLASH_CR_STRT BIT(6)
#define FLASH_CR_LOCK BIT(7)
#define FLASH_CR_OPTWRE BIT(9)
#define FLASH_CR_OBL_LAUNCH BIT(13)
#define STM32_FLASH_OPT_LOCKED (!(STM32_FLASH_CR & FLASH_CR_OPTWRE))
#define STM32_FLASH_AR REG32(STM32_FLASH_REGS_BASE + 0x14)
#define STM32_FLASH_OBR REG32(STM32_FLASH_REGS_BASE + 0x1c)
#define STM32_FLASH_OBR_RDP_MASK (3 << 1)
#define STM32_FLASH_WRPR REG32(STM32_FLASH_REGS_BASE + 0x20)

#define STM32_OPTB_RDP_OFF 0x00
#define STM32_OPTB_USER_OFF 0x02
#define STM32_OPTB_WRP_OFF(n) (0x08 + (n & 3) * 2)
#define STM32_OPTB_WRP01 0x08
#define STM32_OPTB_WRP23 0x0c

#define STM32_OPTB_COMPL_SHIFT 8

/* --- External Interrupts --- */
#define STM32_EXTI_IMR REG32(STM32_EXTI_BASE + 0x00)
#define STM32_EXTI_EMR REG32(STM32_EXTI_BASE + 0x04)
#define STM32_EXTI_RTSR REG32(STM32_EXTI_BASE + 0x08)
#define STM32_EXTI_FTSR REG32(STM32_EXTI_BASE + 0x0c)
#define STM32_EXTI_SWIER REG32(STM32_EXTI_BASE + 0x10)
#define STM32_EXTI_PR REG32(STM32_EXTI_BASE + 0x14)

#define EXTI_PVD_EVENT BIT(16)
#define EXTI_RTC_ALR_EVENT BIT(17)
#define EXTI_COMP2_EVENT BIT(22)

/* --- ADC --- */
#define STM32_ADC_ISR REG32(STM32_ADC1_BASE + 0x00)
#define STM32_ADC_ISR_ADRDY BIT(0)
#define STM32_ADC_IER REG32(STM32_ADC1_BASE + 0x04)
#define STM32_ADC_IER_AWDIE BIT(7)
#define STM32_ADC_IER_OVRIE BIT(4)
#define STM32_ADC_IER_EOSEQIE BIT(3)
#define STM32_ADC_IER_EOCIE BIT(2)
#define STM32_ADC_IER_EOSMPIE BIT(1)
#define STM32_ADC_IER_ADRDYIE BIT(0)

#define STM32_ADC_CR REG32(STM32_ADC1_BASE + 0x08)
#define STM32_ADC_CR_ADEN BIT(0)
#define STM32_ADC_CR_ADDIS BIT(1)
#define STM32_ADC_CR_ADCAL BIT(31)
#define STM32_ADC_CFGR1 REG32(STM32_ADC1_BASE + 0x0C)
/* Analog watchdog channel selection */
#define STM32_ADC_CFGR1_AWDCH_MASK (0x1f << 26)
#define STM32_ADC_CFGR1_AWDEN BIT(23)
#define STM32_ADC_CFGR1_AWDSGL BIT(22)
/* Selects single vs continuous */
#define STM32_ADC_CFGR1_CONT BIT(13)
/* Selects ADC_DR overwrite vs preserve */
#define STM32_ADC_CFGR1_OVRMOD BIT(12)
/* External trigger polarity selection */
#define STM32_ADC_CFGR1_EXTEN_DIS (0 << 10)
#define STM32_ADC_CFGR1_EXTEN_RISE (1 << 10)
#define STM32_ADC_CFGR1_EXTEN_FALL (2 << 10)
#define STM32_ADC_CFGR1_EXTEN_BOTH (3 << 10)
#define STM32_ADC_CFGR1_EXTEN_MASK (3 << 10)
/* External trigger selection */
#define STM32_ADC_CFGR1_TRG0 (0 << 6)
#define STM32_ADC_CFGR1_TRG1 (1 << 6)
#define STM32_ADC_CFGR1_TRG2 (2 << 6)
#define STM32_ADC_CFGR1_TRG3 (3 << 6)
#define STM32_ADC_CFGR1_TRG4 (4 << 6)
#define STM32_ADC_CFGR1_TRG5 (5 << 6)
#define STM32_ADC_CFGR1_TRG6 (6 << 6)
#define STM32_ADC_CFGR1_TRG7 (7 << 6)
#define STM32_ADC_CFGR1_TRG_MASK (7 << 6)
/* Selects circular vs one-shot */
#define STM32_ADC_CFGR1_DMACFG BIT(1)
#define STM32_ADC_CFGR1_DMAEN BIT(0)
#define STM32_ADC_CFGR2 REG32(STM32_ADC1_BASE + 0x10)
/* Sampling time selection - 1.5 ADC cycles min, 239.5 cycles max */
#define STM32_ADC_SMPR REG32(STM32_ADC1_BASE + 0x14)
/* Macro to convert enum stm32_adc_smpr to SMP bits of the ADC_SMPR register */
#define STM32_ADC_SMPR_SMP(s) ((s)-1)
#define STM32_ADC_TR REG32(STM32_ADC1_BASE + 0x20)
#define STM32_ADC_CHSELR REG32(STM32_ADC1_BASE + 0x28)
#define STM32_ADC_DR REG32(STM32_ADC1_BASE + 0x40)
#define STM32_ADC_CCR REG32(STM32_ADC1_BASE + 0x308)

/* --- Comparators --- */
#define STM32_COMP_CSR REG32(STM32_COMP_BASE + 0x1C)

#define STM32_COMP_CMP2LOCK BIT(31)
#define STM32_COMP_CMP2OUT BIT(30)
#define STM32_COMP_CMP2HYST_HI (3 << 28)
#define STM32_COMP_CMP2HYST_MED (2 << 28)
#define STM32_COMP_CMP2HYST_LOW (1 << 28)
#define STM32_COMP_CMP2HYST_NO (0 << 28)
#define STM32_COMP_CMP2POL BIT(27)

#define STM32_COMP_CMP2OUTSEL_TIM3_OCR (7 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM3_IC1 (6 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM2_OCR (5 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM2_IC4 (4 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM1_OCR (3 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM1_IC1 (2 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM1_BRK (1 << 24)
#define STM32_COMP_CMP2OUTSEL_NONE (0 << 24)
#define STM32_COMP_WNDWEN BIT(23)

#define STM32_COMP_CMP2INSEL_MASK (7 << 20)
#define STM32_COMP_CMP2INSEL_INM7 (6 << 20) /* STM32F373 only */
#define STM32_COMP_CMP2INSEL_INM6 (6 << 20)
#define STM32_COMP_CMP2INSEL_INM5 (5 << 20)
#define STM32_COMP_CMP2INSEL_INM4 (4 << 20)
#define STM32_COMP_CMP2INSEL_VREF (3 << 20)
#define STM32_COMP_CMP2INSEL_VREF34 (2 << 20)
#define STM32_COMP_CMP2INSEL_VREF12 (1 << 20)
#define STM32_COMP_CMP2INSEL_VREF14 (0 << 20)

#define STM32_COMP_CMP2MODE_VLSPEED (3 << 18)
#define STM32_COMP_CMP2MODE_LSPEED (2 << 18)
#define STM32_COMP_CMP2MODE_MSPEED (1 << 18)
#define STM32_COMP_CMP2MODE_HSPEED (0 << 18)
#define STM32_COMP_CMP2EN BIT(16)

#define STM32_COMP_CMP1LOCK BIT(15)
#define STM32_COMP_CMP1OUT BIT(14)
#define STM32_COMP_CMP1HYST_HI (3 << 12)
#define STM32_COMP_CMP1HYST_MED (2 << 12)
#define STM32_COMP_CMP1HYST_LOW (1 << 12)
#define STM32_COMP_CMP1HYST_NO (0 << 12)
#define STM32_COMP_CMP1POL BIT(11)

#define STM32_COMP_CMP1OUTSEL_TIM3_OCR (7 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM3_IC1 (6 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM2_OCR (5 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM2_IC4 (4 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM1_OCR (3 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM1_IC1 (2 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM1_BRK (1 << 8)
#define STM32_COMP_CMP1OUTSEL_NONE (0 << 8)

#define STM32_COMP_CMP1INSEL_MASK (7 << 4)
#define STM32_COMP_CMP1INSEL_INM7 (7 << 4) /* STM32F373 only */
#define STM32_COMP_CMP1INSEL_INM6 (6 << 4)
#define STM32_COMP_CMP1INSEL_INM5 (5 << 4)
#define STM32_COMP_CMP1INSEL_INM4 (4 << 4)
#define STM32_COMP_CMP1INSEL_VREF (3 << 4)
#define STM32_COMP_CMP1INSEL_VREF34 (2 << 4)
#define STM32_COMP_CMP1INSEL_VREF12 (1 << 4)
#define STM32_COMP_CMP1INSEL_VREF14 (0 << 4)

#define STM32_COMP_CMP1MODE_VLSPEED (3 << 2)
#define STM32_COMP_CMP1MODE_LSPEED (2 << 2)
#define STM32_COMP_CMP1MODE_MSPEED (1 << 2)
#define STM32_COMP_CMP1MODE_HSPEED (0 << 2)
#define STM32_COMP_CMP1SW1 BIT(1)
#define STM32_COMP_CMP1EN BIT(0)

/* --- DMA --- */

/*
 * Available DMA channels, numbered from 0.
 *
 * Note: The STM datasheet tends to number things from 1. We should ask
 * the European elevator engineers to talk to MCU engineer counterparts
 * about this.  This means that if the datasheet refers to channel n,
 * you need to use STM32_DMAC_CHn (=n-1) in the code.
 *
 * Also note that channels are overloaded; obviously you can only use one
 * function on each channel at a time.
 */
enum dma_channel {
	/* Channel numbers */
	STM32_DMAC_CH1 = 0,
	STM32_DMAC_CH2 = 1,
	STM32_DMAC_CH3 = 2,
	STM32_DMAC_CH4 = 3,
	STM32_DMAC_CH5 = 4,
#if defined(CHIP_VARIANT_STM32F07X) || defined(CHIP_VARIANT_STM32F09X)
	STM32_DMAC_CH6 = 5,
	STM32_DMAC_CH7 = 6,
#endif
/* STM32F09 has two DMAs with 7 & 5 channels, respectively */
#ifdef CHIP_VARIANT_STM32F09X
	STM32_DMAC_CH9 = 8,
	STM32_DMAC_CH10 = 9,
	STM32_DMAC_CH11 = 10,
	STM32_DMAC_CH12 = 11,
	STM32_DMAC_CH13 = 12,
	STM32_DMAC_CH14 = 13,
#endif
	/* Channel functions */
	STM32_DMAC_ADC = STM32_DMAC_CH1,
	STM32_DMAC_SPI1_RX = STM32_DMAC_CH2,
	STM32_DMAC_SPI1_TX = STM32_DMAC_CH3,
	STM32_DMAC_DAC_CH1 = STM32_DMAC_CH2,
	STM32_DMAC_DAC_CH2 = STM32_DMAC_CH3,
	STM32_DMAC_I2C2_TX = STM32_DMAC_CH4,
	STM32_DMAC_I2C2_RX = STM32_DMAC_CH5,
	STM32_DMAC_USART1_TX = STM32_DMAC_CH4,
	STM32_DMAC_USART1_RX = STM32_DMAC_CH5,
#if !defined(CHIP_VARIANT_STM32F03X) && !defined(CHIP_VARIANT_STM32F05X)
	STM32_DMAC_USART2_RX = STM32_DMAC_CH6,
	STM32_DMAC_USART2_TX = STM32_DMAC_CH7,
	STM32_DMAC_I2C1_TX = STM32_DMAC_CH6,
	STM32_DMAC_I2C1_RX = STM32_DMAC_CH7,
	STM32_DMAC_PMSE_ROW = STM32_DMAC_CH6,
	STM32_DMAC_PMSE_COL = STM32_DMAC_CH7,
	STM32_DMAC_SPI2_RX = STM32_DMAC_CH6,
	STM32_DMAC_SPI2_TX = STM32_DMAC_CH7,

	/* Only DMA1 (with 7 channels) is present on STM32L151x */
	STM32_DMAC_COUNT = 7,

#else /* stm32f03x and stm32f05x have only 5 channels */
	STM32_DMAC_COUNT = 5,
#endif
};
/*
 * TODO(b/233369173): This file was originally shared by many MCUs,
 *  8 is assumed to be the max number of channels for all chips.
 */
#define STM32_DMAC_PER_CTLR 8

/* Registers for a single channel of the DMA controller */
struct stm32_dma_chan {
	uint32_t ccr; /* Control */
	uint32_t cndtr; /* Number of data to transfer */
	uint32_t cpar; /* Peripheral address */
	uint32_t cmar; /* Memory address */
	uint32_t reserved;
};

/* Always use stm32_dma_chan_t so volatile keyword is included! */
typedef volatile struct stm32_dma_chan stm32_dma_chan_t;

/* Common code and header file must use this */
typedef stm32_dma_chan_t dma_chan_t;

/* Registers for the DMA controller */
struct stm32_dma_regs {
	uint32_t isr;
	uint32_t ifcr;
	stm32_dma_chan_t chan[STM32_DMAC_COUNT];
};

/* Always use stm32_dma_regs_t so volatile keyword is included! */
typedef volatile struct stm32_dma_regs stm32_dma_regs_t;

#define STM32_DMA1_REGS ((stm32_dma_regs_t *)STM32_DMA1_BASE)

#define STM32_DMA_CCR_CHANNEL(channel) (0)

#ifdef CHIP_VARIANT_STM32F09X
#define STM32_DMA2_REGS ((stm32_dma_regs_t *)STM32_DMA2_BASE)
#define STM32_DMA_REGS(channel) \
	((channel) < STM32_DMAC_PER_CTLR ? STM32_DMA1_REGS : STM32_DMA2_REGS)
#define STM32_DMA_CSELR(channel)                                    \
	REG32(((channel) < STM32_DMAC_PER_CTLR ? STM32_DMA1_BASE :  \
						 STM32_DMA2_BASE) + \
	      0xA8)
#else
#define STM32_DMA_REGS(channel) STM32_DMA1_REGS
#endif

/* Bits for DMA controller regs (isr and ifcr) */
#define STM32_DMA_CH_OFFSET(channel) (4 * ((channel) % STM32_DMAC_PER_CTLR))
#define STM32_DMA_ISR_MASK(channel, mask) \
	((mask) << STM32_DMA_CH_OFFSET(channel))
#define STM32_DMA_ISR_GIF(channel) STM32_DMA_ISR_MASK(channel, BIT(0))
#define STM32_DMA_ISR_TCIF(channel) STM32_DMA_ISR_MASK(channel, BIT(1))
#define STM32_DMA_ISR_HTIF(channel) STM32_DMA_ISR_MASK(channel, BIT(2))
#define STM32_DMA_ISR_TEIF(channel) STM32_DMA_ISR_MASK(channel, BIT(3))
#define STM32_DMA_ISR_ALL(channel) STM32_DMA_ISR_MASK(channel, 0x0f)

#define STM32_DMA_GIF BIT(0)
#define STM32_DMA_TCIF BIT(1)
#define STM32_DMA_HTIF BIT(2)
#define STM32_DMA_TEIF BIT(3)
#define STM32_DMA_ALL 0xf

#define STM32_DMA_GET_ISR(channel)                                        \
	((STM32_DMA_REGS(channel)->isr >> STM32_DMA_CH_OFFSET(channel)) & \
	 STM32_DMA_ALL)
#define STM32_DMA_SET_ISR(channel, val)                                \
	(STM32_DMA_REGS(channel)->isr =                                \
		 ((STM32_DMA_REGS(channel)->isr &                      \
		   ~(STM32_DMA_ALL << STM32_DMA_CH_OFFSET(channel))) | \
		  (((val) & STM32_DMA_ALL) << STM32_DMA_CH_OFFSET(channel))))
#define STM32_DMA_GET_IFCR(channel)                                        \
	((STM32_DMA_REGS(channel)->ifcr >> STM32_DMA_CH_OFFSET(channel)) & \
	 STM32_DMA_ALL)
#define STM32_DMA_SET_IFCR(channel, val)                               \
	(STM32_DMA_REGS(channel)->ifcr =                               \
		 ((STM32_DMA_REGS(channel)->ifcr &                     \
		   ~(STM32_DMA_ALL << STM32_DMA_CH_OFFSET(channel))) | \
		  (((val) & STM32_DMA_ALL) << STM32_DMA_CH_OFFSET(channel))))

/* Bits for DMA channel regs */
#define STM32_DMA_CCR_EN BIT(0)
#define STM32_DMA_CCR_TCIE BIT(1)
#define STM32_DMA_CCR_HTIE BIT(2)
#define STM32_DMA_CCR_TEIE BIT(3)
#define STM32_DMA_CCR_DIR BIT(4)
#define STM32_DMA_CCR_CIRC BIT(5)
#define STM32_DMA_CCR_PINC BIT(6)
#define STM32_DMA_CCR_MINC BIT(7)
#define STM32_DMA_CCR_PSIZE_8_BIT (0 << 8)
#define STM32_DMA_CCR_PSIZE_16_BIT (1 << 8)
#define STM32_DMA_CCR_PSIZE_32_BIT (2 << 8)
#define STM32_DMA_CCR_MSIZE_8_BIT (0 << 10)
#define STM32_DMA_CCR_MSIZE_16_BIT (1 << 10)
#define STM32_DMA_CCR_MSIZE_32_BIT (2 << 10)
#define STM32_DMA_CCR_PL_LOW (0 << 12)
#define STM32_DMA_CCR_PL_MEDIUM (1 << 12)
#define STM32_DMA_CCR_PL_HIGH (2 << 12)
#define STM32_DMA_CCR_PL_VERY_HIGH (3 << 12)
#define STM32_DMA_CCR_MEM2MEM BIT(14)

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
