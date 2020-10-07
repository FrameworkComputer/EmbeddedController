/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Register map for the STM32L4 family of chips
 *
 * This header file should not be included directly.
 * Please include registers.h instead.
 *
 * Known Chip Variants
 * - STM32L442
 * - STM32L476
 */

#ifndef __CROS_EC_REGISTERS_H
#error "This header file should not be included directly."
#endif

/* --- IRQ numbers --- */
#define STM32_IRQ_WWDG             0
#define STM32_IRQ_PVD              1
#define STM32_IRQ_TAMPER_STAMP     2
#define STM32_IRQ_RTC_WAKEUP       3
#define STM32_IRQ_FLASH            4
#define STM32_IRQ_RCC              5
#define STM32_IRQ_EXTI0            6
#define STM32_IRQ_EXTI1            7
#define STM32_IRQ_EXTI2            8
#define STM32_IRQ_EXTI3            9
#define STM32_IRQ_EXTI4           10
#define STM32_IRQ_DMA_CHANNEL_1   11
#define STM32_IRQ_DMA_CHANNEL_2   12
#define STM32_IRQ_DMA_CHANNEL_3   13
#define STM32_IRQ_DMA_CHANNEL_4   14
#define STM32_IRQ_DMA_CHANNEL_5   15
#define STM32_IRQ_DMA_CHANNEL_6   16
#define STM32_IRQ_DMA_CHANNEL_7   17
#define STM32_IRQ_USB_HP          19
#define STM32_IRQ_USB_LP          20

#define STM32_IRQ_ADC1            18 /* STM32L4 only */
#define STM32_IRQ_CAN_TX          19 /* STM32F373 only */
#define STM32_IRQ_USB_LP_CAN_RX   20 /* STM32F373 only */
#define STM32_IRQ_DAC             21
#define STM32_IRQ_CAN_RX1         21 /* STM32F373 only */

#define STM32_IRQ_COMP            22

#define STM32_IRQ_CAN_SCE         22 /* STM32F373 only */
#define STM32_IRQ_EXTI9_5         23
#define STM32_IRQ_LCD             24 /* STM32L15X only */
#define STM32_IRQ_TIM15           24 /* STM32F373 only */
#define STM32_IRQ_TIM9            25 /* STM32L15X only */
#define STM32_IRQ_TIM16           25 /* STM32F373 only */
#define STM32_IRQ_TIM10           26 /* STM32L15X only */
#define STM32_IRQ_TIM17           26 /* STM32F373 only */
#define STM32_IRQ_TIM11           27 /* STM32L15X only */
#define STM32_IRQ_TIM18_DAC2      27 /* STM32F373 only */
#define STM32_IRQ_TIM2            28
#define STM32_IRQ_TIM3            29
#define STM32_IRQ_TIM4            30
#define STM32_IRQ_I2C1_EV         31
#define STM32_IRQ_I2C1_ER         32
#define STM32_IRQ_I2C2_EV         33
#define STM32_IRQ_I2C2_ER         34
#define STM32_IRQ_SPI1            35
#define STM32_IRQ_SPI2            36
#define STM32_IRQ_USART1          37
#define STM32_IRQ_USART2          38
#define STM32_IRQ_USART3          39
#define STM32_IRQ_EXTI15_10       40
#define STM32_IRQ_RTC_ALARM       41
#define STM32_IRQ_USB_FS_WAKEUP   42 /* STM32L15X */
#define STM32_IRQ_CEC             42 /* STM32F373 only */
#define STM32_IRQ_TIM6_BASIC      43 /* STM32L15X only */
#define STM32_IRQ_TIM12           43 /* STM32F373 only */
#define STM32_IRQ_TIM7_BASIC      44 /* STM32L15X only */
#define STM32_IRQ_TIM13           44 /* STM32F373 only */
#define STM32_IRQ_TIM14           45 /* STM32F373 only */
#define STM32_IRQ_TIM5            50 /* STM32F373 */
#define STM32_IRQ_SPI3            51 /* STM32F373 */
#define STM32_IRQ_USART4          52 /* STM32F446 only */
#define STM32_IRQ_USART5          53 /* STM32F446 only */
#define STM32_IRQ_TIM6_DAC        54 /* STM32F373 */
#define STM32_IRQ_TIM7            55 /* STM32F373 */
#define STM32_IRQ_DMA2_CHANNEL1   56 /* STM32F373 */
#define STM32_IRQ_DMA2_CHANNEL2   57 /* STM32F373 */
#define STM32_IRQ_DMA2_CHANNEL3   58 /* STM32F373 */
#define STM32_IRQ_DMA2_CHANNEL4   59 /* STM32F373 only */
/* if MISC_REMAP bits are set */
#define STM32_IRQ_DMA2_CHANNEL5   60 /* STM32F373 */
#define STM32_IRQ_SDADC1          61 /* STM32F373 only */
#define STM32_IRQ_SDADC2          62 /* STM32F373 only */
#define STM32_IRQ_SDADC3          63 /* STM32F373 only */
#define STM32_IRQ_DMA2_CHANNEL6   68 /* STM32L4 only */
#define STM32_IRQ_DMA2_CHANNEL7   69 /* STM32L4 only */
#define STM32_IRQ_LPUART          70 /* STM32L4 only */
#define STM32_IRQ_USART9          70 /* STM32L4 only */
#define STM32_IRQ_USART6          71 /* STM32F446 only */
#define STM32_IRQ_I2C3_EV         72 /* STM32F446 only */
#define STM32_IRQ_I2C3_ER         73 /* STM32F446 only */
#define STM32_IRQ_USB_WAKEUP      76 /* STM32F373 only */
#define STM32_IRQ_TIM19           78 /* STM32F373 only */
#define STM32_IRQ_AES             79 /* STM32L4 only */
#define STM32_IRQ_RNG             80 /* STM32L4 only */
#define STM32_IRQ_FPU             81 /* STM32F373 only */


/* To simplify code generation, define DMA channel 9..10 */
#define STM32_IRQ_DMA_CHANNEL_9    STM32_IRQ_DMA2_CHANNEL1
#define STM32_IRQ_DMA_CHANNEL_10   STM32_IRQ_DMA2_CHANNEL2
#define STM32_IRQ_DMA_CHANNEL_13   STM32_IRQ_DMA2_CHANNEL6
#define STM32_IRQ_DMA_CHANNEL_14   STM32_IRQ_DMA2_CHANNEL7

/* aliases for easier code sharing */
#define STM32_IRQ_I2C1 STM32_IRQ_I2C1_EV
#define STM32_IRQ_I2C2 STM32_IRQ_I2C2_EV
#define STM32_IRQ_I2C3 STM32_IRQ_I2C3_EV



/* Peripheral base addresses */
#define STM32_ADC1_BASE             0x40012400
#define STM32_ADC_BASE              0x40012700 /* STM32L15X only */

#define STM32_CEC_BASE              0x40007800 /* STM32F373 */
#define STM32_CRC_BASE              0x40023000
#define STM32_CRS_BASE              0x40006c00 /* STM32F0XX */
#define STM32_DAC_BASE              0x40007400


#define STM32_DBGMCU_BASE           0xE0042000

#define STM32_DMA1_BASE             0x40020000
#define STM32_DMA2_BASE             0x40020400

#define STM32_EXTI_BASE             0x40010400

#define STM32_FLASH_REGS_BASE       0x40022000

#define STM32_GPIOA_BASE            0x48000000
#define STM32_GPIOB_BASE            0x48000400
#define STM32_GPIOC_BASE            0x48000800
#define STM32_GPIOD_BASE            0x48000C00
#define STM32_GPIOE_BASE            0x48001000
#define STM32_GPIOF_BASE            0x48001400
#define STM32_GPIOG_BASE            0x48001800 /* only for stm32l4x6 */
#define STM32_GPIOH_BASE            0x48001C00 /* only for stm32l4 */

#define STM32_I2C1_BASE             0x40005400
#define STM32_I2C2_BASE             0x40005800
#define STM32_I2C3_BASE             0x40005C00
#define STM32_I2C4_BASE             0x40006000

#define STM32_IWDG_BASE             0x40003000
#define STM32_LCD_BASE              0x40002400

#define STM32_OPTB_BASE             0x1FFF7800

#define STM32_PMSE_BASE             0x40013400
#define STM32_PWR_BASE              0x40007000

#define STM32_RCC_BASE              0x40021000

#define STM32_RI_BASE               0x40007C00 /* STM32L1xx only */
#define STM32_RNG_BASE              0x50060800 /* STM32L4 */
#define STM32_RTC_BASE              0x40002800

#define STM32_SPI1_BASE             0x40013000
#define STM32_SPI2_BASE             0x40003800
#define STM32_SPI3_BASE             0x40003c00 /* STM32F373, STM32L4, STM32F7 */

#define STM32_SYSCFG_BASE           0x40010000

#define STM32_TIM1_BASE             0x40012c00 /* STM32F373 */
#define STM32_TIM2_BASE             0x40000000
#define STM32_TIM3_BASE             0x40000400
#define STM32_TIM4_BASE             0x40000800
#define STM32_TIM5_BASE             0x40000c00 /* STM32F373 */
#define STM32_TIM6_BASE             0x40001000
#define STM32_TIM7_BASE             0x40001400
#define STM32_TIM12_BASE            0x40001800 /* STM32F373 */
#define STM32_TIM13_BASE            0x40001c00 /* STM32F373 */
#define STM32_TIM14_BASE            0x40002000 /* STM32F373 */
#define STM32_TIM15_BASE            0x40014000
#define STM32_TIM16_BASE            0x40014400
#define STM32_TIM17_BASE            0x40014800
#define STM32_TIM18_BASE            0x40009c00 /* STM32F373 only */
#define STM32_TIM19_BASE            0x40015c00 /* STM32F373 only */

#define STM32_UNIQUE_ID_BASE        0x1ffff7ac

#define STM32_USART1_BASE           0x40013800
#define STM32_USART2_BASE           0x40004400
#define STM32_USART3_BASE           0x40004800
#define STM32_USART4_BASE           0x40004c00
#define STM32_USART9_BASE           0x40008000 /* LPUART */

#define STM32_USB_CAN_SRAM_BASE     0x40006000
#define STM32_USB_FS_BASE           0x40005C00

#define STM32_WWDG_BASE             0x40002C00


#ifndef __ASSEMBLER__

/* Register definitions */

/* --- USART --- */
#define STM32_USART_CR1(base)      STM32_USART_REG(base, 0x00)
#define STM32_USART_CR1_UE		BIT(0)
#define STM32_USART_CR1_UESM            BIT(1)
#define STM32_USART_CR1_RE		BIT(2)
#define STM32_USART_CR1_TE		BIT(3)
#define STM32_USART_CR1_RXNEIE		BIT(5)
#define STM32_USART_CR1_TCIE		BIT(6)
#define STM32_USART_CR1_TXEIE		BIT(7)
#define STM32_USART_CR1_PS		BIT(9)
#define STM32_USART_CR1_PCE		BIT(10)
#define STM32_USART_CR1_M		BIT(12)
#define STM32_USART_CR1_OVER8		BIT(15)
#define STM32_USART_CR2(base)      STM32_USART_REG(base, 0x04)
#define STM32_USART_CR2_SWAP		BIT(15)
#define STM32_USART_CR3(base)      STM32_USART_REG(base, 0x08)
#define STM32_USART_CR3_EIE		BIT(0)
#define STM32_USART_CR3_DMAR		BIT(6)
#define STM32_USART_CR3_DMAT		BIT(7)
#define STM32_USART_CR3_ONEBIT		BIT(11)
#define STM32_USART_CR3_OVRDIS		BIT(12)
#define STM32_USART_CR3_WUS_START_BIT	(2 << 20)
#define STM32_USART_CR3_WUFIE		BIT(22)
#define STM32_USART_BRR(base)      STM32_USART_REG(base, 0x0C)
#define STM32_USART_GTPR(base)     STM32_USART_REG(base, 0x10)
#define STM32_USART_RTOR(base)     STM32_USART_REG(base, 0x14)
#define STM32_USART_RQR(base)      STM32_USART_REG(base, 0x18)
#define STM32_USART_ISR(base)      STM32_USART_REG(base, 0x1C)
#define STM32_USART_ICR(base)      STM32_USART_REG(base, 0x20)
#define STM32_USART_ICR_ORECF		BIT(3)
#define STM32_USART_ICR_TCCF		BIT(6)
#define STM32_USART_RDR(base)      STM32_USART_REG(base, 0x24)
#define STM32_USART_TDR(base)      STM32_USART_REG(base, 0x28)
#define STM32_USART_PRESC(base)    STM32_USART_REG(base, 0x2C)
/* register alias */
#define STM32_USART_SR(base)       STM32_USART_ISR(base)
#define STM32_USART_SR_ORE		BIT(3)
#define STM32_USART_SR_RXNE		BIT(5)
#define STM32_USART_SR_TC		BIT(6)
#define STM32_USART_SR_TXE		BIT(7)

/* --- GPIO --- */

#define STM32_GPIO_MODER(b)     REG32((b) + 0x00)
#define STM32_GPIO_OTYPER(b)    REG16((b) + 0x04)
#define STM32_GPIO_OSPEEDR(b)   REG32((b) + 0x08)
#define STM32_GPIO_PUPDR(b)     REG32((b) + 0x0C)
#define STM32_GPIO_IDR(b)       REG16((b) + 0x10)
#define STM32_GPIO_ODR(b)       REG16((b) + 0x14)
#define STM32_GPIO_BSRR(b)      REG32((b) + 0x18)
#define STM32_GPIO_LCKR(b)      REG32((b) + 0x1C)
#define STM32_GPIO_AFRL(b)      REG32((b) + 0x20)
#define STM32_GPIO_AFRH(b)      REG32((b) + 0x24)
#define STM32_GPIO_BRR(b)       REG32((b) + 0x28)
#define STM32_GPIO_ASCR(b)      REG32((b) + 0x2C) /* only for stm32l4 */

#define GPIO_ALT_F0		0x0
#define GPIO_ALT_F1		0x1
#define GPIO_ALT_F2		0x2
#define GPIO_ALT_F3		0x3
#define GPIO_ALT_F4		0x4
#define GPIO_ALT_F5		0x5
#define GPIO_ALT_F6		0x6
#define GPIO_ALT_F7		0x7
#define GPIO_ALT_F8		0x8
#define GPIO_ALT_F9		0x9
#define GPIO_ALT_FA		0xA
#define GPIO_ALT_FB		0xB
#define GPIO_ALT_FC		0xC
#define GPIO_ALT_FD		0xD
#define GPIO_ALT_FE		0xE
#define GPIO_ALT_FF		0xF

/* --- I2C --- */
#define STM32_I2C_CR1(n)            REG32(stm32_i2c_reg(n, 0x00))
#define STM32_I2C_CR1_PE            BIT(0)
#define STM32_I2C_CR1_TXIE          BIT(1)
#define STM32_I2C_CR1_RXIE          BIT(2)
#define STM32_I2C_CR1_ADDRIE        BIT(3)
#define STM32_I2C_CR1_NACKIE        BIT(4)
#define STM32_I2C_CR1_STOPIE        BIT(5)
#define STM32_I2C_CR1_ERRIE         BIT(7)
#define STM32_I2C_CR1_WUPEN         BIT(18)
#define STM32_I2C_CR2(n)            REG32(stm32_i2c_reg(n, 0x04))
#define STM32_I2C_CR2_RD_WRN        BIT(10)
#define STM32_I2C_CR2_START         BIT(13)
#define STM32_I2C_CR2_STOP          BIT(14)
#define STM32_I2C_CR2_NACK          BIT(15)
#define STM32_I2C_CR2_RELOAD        BIT(24)
#define STM32_I2C_CR2_AUTOEND       BIT(25)
#define STM32_I2C_OAR1(n)           REG32(stm32_i2c_reg(n, 0x08))
#define STM32_I2C_OAR2(n)           REG32(stm32_i2c_reg(n, 0x0C))
#define STM32_I2C_TIMINGR(n)        REG32(stm32_i2c_reg(n, 0x10))
#define STM32_I2C_TIMEOUTR(n)       REG32(stm32_i2c_reg(n, 0x14))
#define STM32_I2C_ISR(n)            REG32(stm32_i2c_reg(n, 0x18))
#define STM32_I2C_ISR_TXE           BIT(0)
#define STM32_I2C_ISR_TXIS          BIT(1)
#define STM32_I2C_ISR_RXNE          BIT(2)
#define STM32_I2C_ISR_ADDR          BIT(3)
#define STM32_I2C_ISR_NACK          BIT(4)
#define STM32_I2C_ISR_STOP          BIT(5)
#define STM32_I2C_ISR_TC            BIT(6)
#define STM32_I2C_ISR_TCR           BIT(7)
#define STM32_I2C_ISR_BERR          BIT(8)
#define STM32_I2C_ISR_ARLO          BIT(9)
#define STM32_I2C_ISR_OVR           BIT(10)
#define STM32_I2C_ISR_PECERR        BIT(11)
#define STM32_I2C_ISR_TIMEOUT       BIT(12)
#define STM32_I2C_ISR_ALERT         BIT(13)
#define STM32_I2C_ISR_BUSY          BIT(15)
#define STM32_I2C_ISR_DIR           BIT(16)
#define STM32_I2C_ISR_ADDCODE(isr)  (((isr) >> 16) & 0xfe)
#define STM32_I2C_ICR(n)            REG32(stm32_i2c_reg(n, 0x1C))
#define STM32_I2C_ICR_ADDRCF        BIT(3)
#define STM32_I2C_ICR_NACKCF        BIT(4)
#define STM32_I2C_ICR_STOPCF        BIT(5)
#define STM32_I2C_ICR_BERRCF        BIT(8)
#define STM32_I2C_ICR_ARLOCF        BIT(9)
#define STM32_I2C_ICR_OVRCF         BIT(10)
#define STM32_I2C_ICR_TIMEOUTCF     BIT(12)
#define STM32_I2C_ICR_ALL           0x3F38
#define STM32_I2C_PECR(n)           REG32(stm32_i2c_reg(n, 0x20))
#define STM32_I2C_RXDR(n)           REG32(stm32_i2c_reg(n, 0x24))
#define STM32_I2C_TXDR(n)           REG32(stm32_i2c_reg(n, 0x28))


/* --- Power / Reset / Clocks --- */
#define STM32_PWR_CR2               REG32(STM32_PWR_BASE + 0x04)
#define STM32_PWR_CSR               REG32(STM32_PWR_BASE + 0x10)


#define STM32_RCC_CR			REG32(STM32_RCC_BASE + 0x00)
#define STM32_RCC_CR_MSION		BIT(0)
#define STM32_RCC_CR_MSIRDY		BIT(1)
#define STM32_RCC_CR_HSION		BIT(8)
#define STM32_RCC_CR_HSIRDY		BIT(10)
#define STM32_RCC_CR_HSEON		BIT(16)
#define STM32_RCC_CR_HSERDY		BIT(17)
#define STM32_RCC_CR_PLLON		BIT(24)
#define STM32_RCC_CR_PLLRDY		BIT(25)

#define STM32_RCC_ICSCR			REG32(STM32_RCC_BASE + 0x04)
#define STM32_RCC_ICSCR_MSIRANGE(n)	((n) << 13)
#define STM32_RCC_ICSCR_MSIRANGE_1MHZ	STM32_RCC_ICSCR_MSIRANGE(4)
#define STM32_RCC_ICSCR_MSIRANGE_2MHZ	STM32_RCC_ICSCR_MSIRANGE(5)
#define STM32_RCC_ICSCR_MSIRANGE_MASK	STM32_RCC_ICSCR_MSIRANGE(7)

#define STM32_RCC_CFGR			REG32(STM32_RCC_BASE + 0x08)
#define STM32_RCC_CFGR_SW_MSI		(0 << 0)
#define STM32_RCC_CFGR_SW_HSI		(1 << 0)
#define STM32_RCC_CFGR_SW_HSE		(2 << 0)
#define STM32_RCC_CFGR_SW_PLL		(3 << 0)
#define STM32_RCC_CFGR_SW_MASK		(3 << 0)
#define STM32_RCC_CFGR_SWS_MSI		(0 << 2)
#define STM32_RCC_CFGR_SWS_HSI		(1 << 2)
#define STM32_RCC_CFGR_SWS_HSE		(2 << 2)
#define STM32_RCC_CFGR_SWS_PLL		(3 << 2)
#define STM32_RCC_CFGR_SWS_MASK		(3 << 2)

#define STM32_RCC_PLLCFGR		REG32(STM32_RCC_BASE + 0x0C)
#define STM32_RCC_PLLCFGR_PLLSRC_SHIFT	(0)
#define STM32_RCC_PLLCFGR_PLLSRC_NONE	(0 << STM32_RCC_PLLCFGR_PLLSRC_SHIFT)
#define STM32_RCC_PLLCFGR_PLLSRC_MSI	(1 << STM32_RCC_PLLCFGR_PLLSRC_SHIFT)
#define STM32_RCC_PLLCFGR_PLLSRC_HSI	(2 << STM32_RCC_PLLCFGR_PLLSRC_SHIFT)
#define STM32_RCC_PLLCFGR_PLLSRC_HSE	(3 << STM32_RCC_PLLCFGR_PLLSRC_SHIFT)
#define STM32_RCC_PLLCFGR_PLLSRC_MASK	(3 << STM32_RCC_PLLCFGR_PLLSRC_SHIFT)
#define STM32_RCC_PLLCFGR_PLLM_SHIFT	(4)
#define STM32_RCC_PLLCFGR_PLLM_MASK	(0x7 << STM32_RCC_PLLCFGR_PLLM_SHIFT)
#define STM32_RCC_PLLCFGR_PLLN_SHIFT	(8)
#define STM32_RCC_PLLCFGR_PLLN_MASK	(0x7f << STM32_RCC_PLLCFGR_PLLN_SHIFT)
#define STM32_RCC_PLLCFGR_PLLREN_SHIFT	(24)
#define STM32_RCC_PLLCFGR_PLLREN_MASK	(1 << STM32_RCC_PLLCFGR_PLLREN_SHIFT)
#define STM32_RCC_PLLCFGR_PLLR_SHIFT	(25)
#define STM32_RCC_PLLCFGR_PLLR_MASK	(3 << STM32_RCC_PLLCFGR_PLLR_SHIFT)

#define STM32_RCC_AHB1RSTR              REG32(STM32_RCC_BASE + 0x28)
#define STM32_RCC_AHB2RSTR              REG32(STM32_RCC_BASE + 0x2C)
#define STM32_RCC_AHB3RSTR              REG32(STM32_RCC_BASE + 0x30)
#define STM32_RCC_APB1RSTR1             REG32(STM32_RCC_BASE + 0x38)
#define STM32_RCC_APB1RSTR2             REG32(STM32_RCC_BASE + 0x3C)
#define STM32_RCC_APB2RSTR              REG32(STM32_RCC_BASE + 0x40)

#define STM32_RCC_AHB1ENR		REG32(STM32_RCC_BASE + 0x48)
#define STM32_RCC_AHB1ENR_DMA1EN	BIT(0)
#define STM32_RCC_AHB1ENR_DMA2EN	BIT(1)

#define STM32_RCC_AHB2ENR		REG32(STM32_RCC_BASE + 0x4C)
#define STM32_RCC_AHB2ENR_GPIOMASK	(0xff << 0)
#define STM32_RCC_AHB2ENR_RNGEN		BIT(18)

#define STM32_RCC_APB1ENR		REG32(STM32_RCC_BASE + 0x58)
#define STM32_RCC_PWREN                 BIT(28)

#define STM32_RCC_APB1ENR2		REG32(STM32_RCC_BASE + 0x5C)
#define STM32_RCC_APB1ENR2_LPUART1EN	BIT(0)

#define STM32_RCC_APB2ENR		REG32(STM32_RCC_BASE + 0x60)
#define STM32_RCC_SYSCFGEN		BIT(0)

#define STM32_RCC_CCIPR			REG32(STM32_RCC_BASE + 0x88)
#define STM32_RCC_CCIPR_USART1SEL_SHIFT (0)
#define STM32_RCC_CCIPR_USART1SEL_MASK  (3 << STM32_RCC_CCIPR_USART1SEL_SHIFT)
#define STM32_RCC_CCIPR_USART2SEL_SHIFT (2)
#define STM32_RCC_CCIPR_USART2SEL_MASK  (3 << STM32_RCC_CCIPR_USART2SEL_SHIFT)
#define STM32_RCC_CCIPR_USART3SEL_SHIFT (4)
#define STM32_RCC_CCIPR_USART3SEL_MASK  (3 << STM32_RCC_CCIPR_USART3SEL_SHIFT)
#define STM32_RCC_CCIPR_UART4SEL_SHIFT (6)
#define STM32_RCC_CCIPR_UART4SEL_MASK  (3 << STM32_RCC_CCIPR_UART4SEL_SHIFT)
#define STM32_RCC_CCIPR_UART5SEL_SHIFT (8)
#define STM32_RCC_CCIPR_UART5SEL_MASK  (3 << STM32_RCC_CCIPR_UART5SEL_SHIFT)
#define STM32_RCC_CCIPR_LPUART1SEL_SHIFT (10)
#define STM32_RCC_CCIPR_LPUART1SEL_MASK  (3 << STM32_RCC_CCIPR_LPUART1SEL_SHIFT)
#define STM32_RCC_CCIPR_I2C1SEL_SHIFT (12)
#define STM32_RCC_CCIPR_I2C1SEL_MASK  (3 << STM32_RCC_CCIPR_I2C1SEL_SHIFT)
#define STM32_RCC_CCIPR_I2C2SEL_SHIFT (14)
#define STM32_RCC_CCIPR_I2C2SEL_MASK  (3 << STM32_RCC_CCIPR_I2C2SEL_SHIFT)
#define STM32_RCC_CCIPR_I2C3SEL_SHIFT (16)
#define STM32_RCC_CCIPR_I2C3SEL_MASK  (3 << STM32_RCC_CCIPR_I2C3SEL_SHIFT)
#define STM32_RCC_CCIPR_LPTIM1SEL_SHIFT (18)
#define STM32_RCC_CCIPR_LPTIM1SEL_MASK  (3 << STM32_RCC_CCIPR_LPTIM1SEL_SHIFT)
#define STM32_RCC_CCIPR_LPTIM2SEL_SHIFT (20)
#define STM32_RCC_CCIPR_LPTIM2SEL_MASK  (3 << STM32_RCC_CCIPR_LPTIM2SEL_SHIFT)
#define STM32_RCC_CCIPR_SAI1SEL_SHIFT (22)
#define STM32_RCC_CCIPR_SAI1SEL_MASK  (3 << STM32_RCC_CCIPR_SAI1SEL_SHIFT)
#define STM32_RCC_CCIPR_SAI2SEL_SHIFT (24)
#define STM32_RCC_CCIPR_SAI2SEL_MASK  (3 << STM32_RCC_CCIPR_SAI2SEL_SHIFT)
#define STM32_RCC_CCIPR_CLK48SEL_SHIFT (26)
#define STM32_RCC_CCIPR_CLK48SEL_MASK  (3 << STM32_RCC_CCIPR_CLK48SEL_SHIFT)
#define STM32_RCC_CCIPR_ADCSEL_SHIFT (28)
#define STM32_RCC_CCIPR_ADCSEL_MASK  (3 << STM32_RCC_CCIPR_ADCSEL_SHIFT)
#define STM32_RCC_CCIPR_SWPMI1SEL_SHIFT (30)
#define STM32_RCC_CCIPR_SWPMI1SEL_MASK  BIT(STM32_RCC_CCIPR_SWPMI1SEL_SHIFT)
#define STM32_RCC_CCIPR_DFSDM1SEL_SHIFT (31)
#define STM32_RCC_CCIPR_DFSDM1SEL_MASK  BIT(STM32_RCC_CCIPR_DFSDM1SEL_SHIFT)

/* Possible clock sources for each peripheral */
#define STM32_RCC_CCIPR_UART_PCLK 	0
#define STM32_RCC_CCIPR_UART_SYSCLK	1
#define STM32_RCC_CCIPR_UART_HSI16	2
#define STM32_RCC_CCIPR_UART_LSE	3

#define STM32_RCC_CCIPR_I2C_PCLK	0
#define STM32_RCC_CCIPR_I2C_SYSCLK	1
#define STM32_RCC_CCIPR_I2C_HSI16	2

#define STM32_RCC_CCIPR_LPTIM_PCLK	0
#define STM32_RCC_CCIPR_LPTIM_LSI	1
#define STM32_RCC_CCIPR_LPTIM_HSI16	2
#define STM32_RCC_CCIPR_LPTIM_LSE	3

#define STM32_RCC_CCIPR_SAI_PLLSAI1CLK	0
#define STM32_RCC_CCIPR_SAI_PLLSAI2CLK	1
#define STM32_RCC_CCIPR_SAI_PLLSAI3CLK	2
#define STM32_RCC_CCIPR_SAI_EXTCLK		3

#define STM32_RCC_CCIPR_CLK48_NONE			0
#define STM32_RCC_CCIPR_CLK48_PLL48M2CLK	1
#define STM32_RCC_CCIPR_CLK48_PLL48M1CLK	2
#define STM32_RCC_CCIPR_CLK48_MSI			3

#define STM32_RCC_CCIPR_ADC_NONE		0
#define STM32_RCC_CCIPR_ADC_PLLADC1CLK	1
#define STM32_RCC_CCIPR_ADC_PLLADC2CLK	2
#define STM32_RCC_CCIPR_ADC_SYSCLK	3

#define STM32_RCC_CCIPR_SWPMI_PCLK	0
#define STM32_RCC_CCIPR_SWPMI_HSI16	1

#define STM32_RCC_CCIPR_DFSDM_PCLK		0
#define STM32_RCC_CCIPR_DFSDM_SYSCLK	1

#define STM32_RCC_BDCR			REG32(STM32_RCC_BASE + 0x90)

#define STM32_RCC_CSR			REG32(STM32_RCC_BASE + 0x94)

#define STM32_RCC_CRRCR			REG32(STM32_RCC_BASE + 0x98)

#define STM32_RCC_CRRCR_HSI48ON         BIT(0)
#define STM32_RCC_CRRCR_HSI48RDY        BIT(1)
#define STM32_RCC_CRRCR_HSI48CAL_MASK   (0x1ff << 7)

#define STM32_RCC_PB2_TIM1		BIT(11)
#define STM32_RCC_PB2_TIM8		BIT(13)

#define STM32_SYSCFG_EXTICR(n)		REG32(STM32_SYSCFG_BASE + 8 + 4 * (n))


/* Peripheral bits for RCC_APB/AHB and DBGMCU regs */
#define STM32_RCC_PB2_USART1		BIT(14)

/* Reset causes definitions */
/* Reset causes in RCC CSR register */
#define STM32_RCC_RESET_CAUSE STM32_RCC_CSR
#define  RESET_CAUSE_WDG                0x60000000
#define  RESET_CAUSE_SFT                0x10000000
#define  RESET_CAUSE_POR                0x08000000
#define  RESET_CAUSE_PIN                0x04000000
#define  RESET_CAUSE_OTHER              0xfe000000
#define  RESET_CAUSE_RMVF               0x01000000
/* Power cause in PWR CSR register */
#define STM32_PWR_RESET_CAUSE STM32_PWR_CSR
#define STM32_PWR_RESET_CAUSE_CLR STM32_PWR_CR
#define  RESET_CAUSE_SBF                0x00000002
#define  RESET_CAUSE_SBF_CLR            0x00000004

/* --- Watchdogs --- */

/* --- Real-Time Clock --- */
#define STM32_RTC_TR                REG32(STM32_RTC_BASE + 0x00)
#define STM32_RTC_DR                REG32(STM32_RTC_BASE + 0x04)
#define STM32_RTC_CR                REG32(STM32_RTC_BASE + 0x08)
#define STM32_RTC_CR_BYPSHAD        BIT(5)
#define STM32_RTC_CR_ALRAE          BIT(8)
#define STM32_RTC_CR_ALRAIE         BIT(12)
#define STM32_RTC_ISR               REG32(STM32_RTC_BASE + 0x0C)
#define STM32_RTC_ISR_ALRAWF        BIT(0)
#define STM32_RTC_ISR_RSF           BIT(5)
#define STM32_RTC_ISR_INITF         BIT(6)
#define STM32_RTC_ISR_INIT          BIT(7)
#define STM32_RTC_ISR_ALRAF         BIT(8)
#define STM32_RTC_PRER              REG32(STM32_RTC_BASE + 0x10)
#define STM32_RTC_PRER_A_MASK       (0x7f << 16)
#define STM32_RTC_PRER_S_MASK       (0x7fff << 0)
#define STM32_RTC_WUTR              REG32(STM32_RTC_BASE + 0x14)
#define STM32_RTC_CALIBR            REG32(STM32_RTC_BASE + 0x18)
#define STM32_RTC_ALRMAR            REG32(STM32_RTC_BASE + 0x1C)
#define STM32_RTC_ALRMBR            REG32(STM32_RTC_BASE + 0x20)
#define STM32_RTC_WPR               REG32(STM32_RTC_BASE + 0x24)
#define STM32_RTC_SSR               REG32(STM32_RTC_BASE + 0x28)
#define STM32_RTC_TSTR              REG32(STM32_RTC_BASE + 0x30)
#define STM32_RTC_TSDR              REG32(STM32_RTC_BASE + 0x34)
#define STM32_RTC_TAFCR             REG32(STM32_RTC_BASE + 0x40)
#define STM32_RTC_ALRMASSR          REG32(STM32_RTC_BASE + 0x44)
#define STM32_RTC_BACKUP(n)         REG32(STM32_RTC_BASE + 0x50 + 4 * (n))

#define STM32_BKP_DATA(n)           STM32_RTC_BACKUP(n)
#define STM32_BKP_BYTES             128

/* --- SPI --- */

/* The SPI controller registers */
struct stm32_spi_regs {
	uint16_t cr1;
	uint16_t _pad0;
	uint16_t cr2;
	uint16_t _pad1;
	unsigned sr;
	uint8_t dr;
	uint8_t _pad2;
	uint16_t _pad3;
	unsigned crcpr;
	unsigned rxcrcr;
	unsigned txcrcr;
	unsigned i2scfgr;	/* STM32L only */
	unsigned i2spr;		/* STM32L only */
};
/* Must be volatile, or compiler optimizes out repeated accesses */
typedef volatile struct stm32_spi_regs stm32_spi_regs_t;

#define STM32_SPI1_REGS ((stm32_spi_regs_t *)STM32_SPI1_BASE)
#define STM32_SPI2_REGS ((stm32_spi_regs_t *)STM32_SPI2_BASE)
#define STM32_SPI3_REGS ((stm32_spi_regs_t *)STM32_SPI3_BASE)
#define STM32_SPI4_REGS ((stm32_spi_regs_t *)STM32_SPI4_BASE)

#define STM32_SPI_CR1_BIDIMODE		BIT(15)
#define STM32_SPI_CR1_BIDIOE		BIT(14)
#define STM32_SPI_CR1_CRCEN		BIT(13)
#define STM32_SPI_CR1_SSM		BIT(9)
#define STM32_SPI_CR1_SSI		BIT(8)
#define STM32_SPI_CR1_LSBFIRST		BIT(7)
#define STM32_SPI_CR1_SPE		BIT(6)
#define STM32_SPI_CR1_BR_DIV64R		(5 << 3)
#define STM32_SPI_CR1_BR_DIV4R		BIT(3)
#define STM32_SPI_CR1_MSTR		BIT(2)
#define STM32_SPI_CR1_CPOL		BIT(1)
#define STM32_SPI_CR1_CPHA		BIT(0)
#define STM32_SPI_CR2_FRXTH		BIT(12)
#define STM32_SPI_CR2_DATASIZE(n)	(((n) - 1) << 8)
#define STM32_SPI_CR2_TXEIE		BIT(7)
#define STM32_SPI_CR2_RXNEIE		BIT(6)
#define STM32_SPI_CR2_NSSP		BIT(3)
#define STM32_SPI_CR2_SSOE		BIT(2)
#define STM32_SPI_CR2_TXDMAEN		BIT(1)
#define STM32_SPI_CR2_RXDMAEN		BIT(0)

#define STM32_SPI_SR_RXNE		BIT(0)
#define STM32_SPI_SR_TXE		BIT(1)
#define STM32_SPI_SR_CRCERR		BIT(4)
#define STM32_SPI_SR_BSY		BIT(7)
#define STM32_SPI_SR_FRLVL		(3 << 9)
#define STM32_SPI_SR_FTLVL		(3 << 11)
/* --- Debug --- */
#define STM32_DBGMCU_APB1FZ         REG32(STM32_DBGMCU_BASE + 0x08)
#define STM32_DBGMCU_APB2FZ         REG32(STM32_DBGMCU_BASE + 0x0C)

/* --- Flash --- */
#define STM32_FLASH_ACR             REG32(STM32_FLASH_REGS_BASE + 0x00)
#define STM32_FLASH_ACR_LATENCY_SHIFT (0)
#define STM32_FLASH_ACR_LATENCY_MASK  (7 << STM32_FLASH_ACR_LATENCY_SHIFT)
#define STM32_FLASH_ACR_PRFTEN      BIT(8)
#define STM32_FLASH_ACR_ICEN        BIT(9)
#define STM32_FLASH_ACR_DCEN        BIT(10)
#define STM32_FLASH_ACR_ICRST       BIT(11)
#define STM32_FLASH_ACR_DCRST       BIT(12)
#define STM32_FLASH_PDKEYR          REG32(STM32_FLASH_REGS_BASE + 0x04)
#define STM32_FLASH_KEYR            REG32(STM32_FLASH_REGS_BASE + 0x08)
#define  FLASH_KEYR_KEY1            0x45670123
#define  FLASH_KEYR_KEY2            0xCDEF89AB
#define STM32_FLASH_OPTKEYR         REG32(STM32_FLASH_REGS_BASE + 0x0c)
#define  FLASH_OPTKEYR_KEY1         0x08192A3B
#define  FLASH_OPTKEYR_KEY2         0x4C5D6E7F
#define STM32_FLASH_SR              REG32(STM32_FLASH_REGS_BASE + 0x10)
#define  FLASH_SR_BUSY              BIT(16)
#define  FLASH_SR_ERR_MASK          (0xc3fa)
#define STM32_FLASH_CR              REG32(STM32_FLASH_REGS_BASE + 0x14)
#define  FLASH_CR_PG                BIT(0)
#define  FLASH_CR_PER               BIT(1)
#define  FLASH_CR_STRT              BIT(16)
#define  FLASH_CR_OPTSTRT           BIT(17)
#define  FLASH_CR_OBL_LAUNCH        BIT(27)
#define  FLASH_CR_OPTLOCK           BIT(30)
#define  FLASH_CR_LOCK              BIT(31)
#define  FLASH_CR_PNB(sec)          (((sec) & 0xff) << 3)
#define  FLASH_CR_PNB_MASK          FLASH_CR_PNB(0xff)
#define STM32_FLASH_ECCR            REG32(STM32_FLASH_REGS_BASE + 0x18)
#define STM32_FLASH_OPTR            REG32(STM32_FLASH_REGS_BASE + 0x20)
#define STM32_FLASH_PCROP1SR        REG32(STM32_FLASH_REGS_BASE + 0x24)
#define STM32_FLASH_PCROP1ER        REG32(STM32_FLASH_REGS_BASE + 0x28)
#define STM32_FLASH_WRP1AR          REG32(STM32_FLASH_REGS_BASE + 0x2C)
#define STM32_FLASH_WRP1BR          REG32(STM32_FLASH_REGS_BASE + 0x30)
/* Minimum number of bytes that can be written to flash */
#define STM32_FLASH_MIN_WRITE_SIZE  CONFIG_FLASH_WRITE_SIZE

#define STM32_OPTB_USER_RDP         REG32(STM32_OPTB_BASE + 0x00)
#define STM32_OPTB_WRP1AR           REG32(STM32_OPTB_BASE + 0x18)
#define STM32_OPTB_WRP1BR           REG32(STM32_OPTB_BASE + 0x20)

/* --- External Interrupts --- */
#define STM32_EXTI_IMR              REG32(STM32_EXTI_BASE + 0x00)
#define STM32_EXTI_EMR              REG32(STM32_EXTI_BASE + 0x04)
#define STM32_EXTI_RTSR             REG32(STM32_EXTI_BASE + 0x08)
#define STM32_EXTI_FTSR             REG32(STM32_EXTI_BASE + 0x0c)
#define STM32_EXTI_SWIER            REG32(STM32_EXTI_BASE + 0x10)
#define STM32_EXTI_PR               REG32(STM32_EXTI_BASE + 0x14)


/* --- ADC --- */

/* --- Comparators --- */


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
	STM32_DMAC_CH6 = 5,
	STM32_DMAC_CH7 = 6,
	/*
	 * Skip CH8, it should belong to DMA engine 1.
	 * Sharing code with STM32s that have 16 engines will be easier.
	 */
	STM32_DMAC_CH9 = 8,
	STM32_DMAC_CH10 = 9,
	STM32_DMAC_CH11 = 10,
	STM32_DMAC_CH12 = 11,
	STM32_DMAC_CH13 = 12,
	STM32_DMAC_CH14 = 13,

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
	STM32_DMAC_USART2_RX = STM32_DMAC_CH6,
	STM32_DMAC_USART2_TX = STM32_DMAC_CH7,
	STM32_DMAC_I2C1_TX = STM32_DMAC_CH6,
	STM32_DMAC_I2C1_RX = STM32_DMAC_CH7,
	STM32_DMAC_PMSE_ROW = STM32_DMAC_CH6,
	STM32_DMAC_PMSE_COL = STM32_DMAC_CH7,
	STM32_DMAC_SPI2_RX = STM32_DMAC_CH4,
	STM32_DMAC_SPI2_TX = STM32_DMAC_CH5,
	STM32_DMAC_SPI3_RX = STM32_DMAC_CH9,
	STM32_DMAC_SPI3_TX = STM32_DMAC_CH10,
	STM32_DMAC_COUNT = 14,
};

#define STM32_DMAC_PER_CTLR 8

/* Registers for a single channel of the DMA controller */
struct stm32_dma_chan {
	uint32_t	ccr;		/* Control */
	uint32_t	cndtr;		/* Number of data to transfer */
	uint32_t	cpar;		/* Peripheral address */
	uint32_t	cmar;		/* Memory address */
	uint32_t	reserved;
};

/* Always use stm32_dma_chan_t so volatile keyword is included! */
typedef volatile struct stm32_dma_chan stm32_dma_chan_t;

/* Common code and header file must use this */
typedef stm32_dma_chan_t dma_chan_t;

/* Registers for the DMA controller */
struct stm32_dma_regs {
	uint32_t	isr;
	uint32_t	ifcr;
	stm32_dma_chan_t chan[STM32_DMAC_COUNT];
};

/* Always use stm32_dma_regs_t so volatile keyword is included! */
typedef volatile struct stm32_dma_regs stm32_dma_regs_t;

#define STM32_DMA1_REGS ((stm32_dma_regs_t *)STM32_DMA1_BASE)


#define STM32_DMA_CCR_CHANNEL(channel)		 (0)
#define STM32_DMA2_REGS ((stm32_dma_regs_t *)STM32_DMA2_BASE)
#define STM32_DMA_REGS(channel) \
	((channel) < STM32_DMAC_PER_CTLR ? STM32_DMA1_REGS : STM32_DMA2_REGS)
#define STM32_DMA_CSELR(channel) \
	REG32(((channel) < STM32_DMAC_PER_CTLR ? \
			STM32_DMA1_BASE : STM32_DMA2_BASE)  + 0xA8)

/* Bits for DMA controller regs (isr and ifcr) */
#define STM32_DMA_CH_OFFSET(channel)   (4 * ((channel) % STM32_DMAC_PER_CTLR))
#define STM32_DMA_ISR_MASK(channel, mask) \
	((mask) << STM32_DMA_CH_OFFSET(channel))
#define STM32_DMA_ISR_GIF(channel)	STM32_DMA_ISR_MASK(channel, BIT(0))
#define STM32_DMA_ISR_TCIF(channel)	STM32_DMA_ISR_MASK(channel, BIT(1))
#define STM32_DMA_ISR_HTIF(channel)	STM32_DMA_ISR_MASK(channel, BIT(2))
#define STM32_DMA_ISR_TEIF(channel)	STM32_DMA_ISR_MASK(channel, BIT(3))
#define STM32_DMA_ISR_ALL(channel)	STM32_DMA_ISR_MASK(channel, 0x0f)

#define STM32_DMA_GIF                   BIT(0)
#define STM32_DMA_TCIF                  BIT(1)
#define STM32_DMA_HTIF                  BIT(2)
#define STM32_DMA_TEIF                  BIT(3)
#define STM32_DMA_ALL                   0xf

#define STM32_DMA_GET_ISR(channel)      \
	((STM32_DMA_REGS(channel)->isr >> STM32_DMA_CH_OFFSET(channel)) \
	& STM32_DMA_ALL)
#define STM32_DMA_SET_ISR(channel, val) \
	(STM32_DMA_REGS(channel)->isr = \
	((STM32_DMA_REGS(channel)->isr & \
	~(STM32_DMA_ALL << STM32_DMA_CH_OFFSET(channel))) | \
	(((val) & STM32_DMA_ALL) << STM32_DMA_CH_OFFSET(channel))))
#define STM32_DMA_GET_IFCR(channel)      \
	((STM32_DMA_REGS(channel)->ifcr >> STM32_DMA_CH_OFFSET(channel)) \
	& STM32_DMA_ALL)
#define STM32_DMA_SET_IFCR(channel, val) \
	(STM32_DMA_REGS(channel)->ifcr = \
	((STM32_DMA_REGS(channel)->ifcr & \
	~(STM32_DMA_ALL << STM32_DMA_CH_OFFSET(channel))) | \
	(((val) & STM32_DMA_ALL) << STM32_DMA_CH_OFFSET(channel))))


/* Bits for DMA channel regs */
#define STM32_DMA_CCR_EN		BIT(0)
#define STM32_DMA_CCR_TCIE		BIT(1)
#define STM32_DMA_CCR_HTIE		BIT(2)
#define STM32_DMA_CCR_TEIE		BIT(3)
#define STM32_DMA_CCR_DIR		BIT(4)
#define STM32_DMA_CCR_CIRC		BIT(5)
#define STM32_DMA_CCR_PINC		BIT(6)
#define STM32_DMA_CCR_MINC		BIT(7)
#define STM32_DMA_CCR_PSIZE_8_BIT	(0 << 8)
#define STM32_DMA_CCR_PSIZE_16_BIT	(1 << 8)
#define STM32_DMA_CCR_PSIZE_32_BIT	(2 << 8)
#define STM32_DMA_CCR_MSIZE_8_BIT	(0 << 10)
#define STM32_DMA_CCR_MSIZE_16_BIT	(1 << 10)
#define STM32_DMA_CCR_MSIZE_32_BIT	(2 << 10)
#define STM32_DMA_CCR_PL_LOW		(0 << 12)
#define STM32_DMA_CCR_PL_MEDIUM		(1 << 12)
#define STM32_DMA_CCR_PL_HIGH		(2 << 12)
#define STM32_DMA_CCR_PL_VERY_HIGH	(3 << 12)
#define STM32_DMA_CCR_MEM2MEM		BIT(14)


/* --- CRC --- */
#define STM32_CRC_DR                REG32(STM32_CRC_BASE + 0x0)
#define STM32_CRC_DR32              REG32(STM32_CRC_BASE + 0x0)
#define STM32_CRC_DR16              REG16(STM32_CRC_BASE + 0x0)
#define STM32_CRC_DR8               REG8(STM32_CRC_BASE + 0x0)

#define STM32_CRC_IDR               REG32(STM32_CRC_BASE + 0x4)
#define STM32_CRC_CR                REG32(STM32_CRC_BASE + 0x8)
#define STM32_CRC_INIT              REG32(STM32_CRC_BASE + 0x10)
#define STM32_CRC_POL               REG32(STM32_CRC_BASE + 0x14)

#define STM32_CRC_CR_RESET          BIT(0)
#define STM32_CRC_CR_POLYSIZE_32    (0 << 3)
#define STM32_CRC_CR_POLYSIZE_16    (1 << 3)
#define STM32_CRC_CR_POLYSIZE_8     (2 << 3)
#define STM32_CRC_CR_POLYSIZE_7     (3 << 3)
#define STM32_CRC_CR_REV_IN_BYTE    (1 << 5)
#define STM32_CRC_CR_REV_IN_HWORD   (2 << 5)
#define STM32_CRC_CR_REV_IN_WORD    (3 << 5)
#define STM32_CRC_CR_REV_OUT        BIT(7)

/* --- PMSE --- */
#define STM32_PMSE_ARCR             REG32(STM32_PMSE_BASE + 0x0)
#define STM32_PMSE_ACCR             REG32(STM32_PMSE_BASE + 0x4)
#define STM32_PMSE_CR               REG32(STM32_PMSE_BASE + 0x8)
#define STM32_PMSE_CRTDR            REG32(STM32_PMSE_BASE + 0x14)
#define STM32_PMSE_IER              REG32(STM32_PMSE_BASE + 0x18)
#define STM32_PMSE_SR               REG32(STM32_PMSE_BASE + 0x1c)
#define STM32_PMSE_IFCR             REG32(STM32_PMSE_BASE + 0x20)
#define STM32_PMSE_PxPMR(x)         REG32(STM32_PMSE_BASE + 0x2c + (x) * 4)
#define STM32_PMSE_PAPMR            REG32(STM32_PMSE_BASE + 0x2c)
#define STM32_PMSE_PBPMR            REG32(STM32_PMSE_BASE + 0x30)
#define STM32_PMSE_PCPMR            REG32(STM32_PMSE_BASE + 0x34)
#define STM32_PMSE_PDPMR            REG32(STM32_PMSE_BASE + 0x38)
#define STM32_PMSE_PEPMR            REG32(STM32_PMSE_BASE + 0x3c)
#define STM32_PMSE_PFPMR            REG32(STM32_PMSE_BASE + 0x40)
#define STM32_PMSE_PGPMR            REG32(STM32_PMSE_BASE + 0x44)
#define STM32_PMSE_PHPMR            REG32(STM32_PMSE_BASE + 0x48)
#define STM32_PMSE_PIPMR            REG32(STM32_PMSE_BASE + 0x4c)
#define STM32_PMSE_MRCR             REG32(STM32_PMSE_BASE + 0x100)
#define STM32_PMSE_MCCR             REG32(STM32_PMSE_BASE + 0x104)

/* --- USB --- */
#define STM32_USB_EP(n)            REG16(STM32_USB_FS_BASE + (n) * 4)

#define STM32_USB_CNTR             REG16(STM32_USB_FS_BASE + 0x40)

#define STM32_USB_CNTR_FRES	    BIT(0)
#define STM32_USB_CNTR_PDWN	    BIT(1)
#define STM32_USB_CNTR_LP_MODE	    BIT(2)
#define STM32_USB_CNTR_FSUSP	    BIT(3)
#define STM32_USB_CNTR_RESUME	    BIT(4)
#define STM32_USB_CNTR_L1RESUME	    BIT(5)
#define STM32_USB_CNTR_L1REQM	    BIT(7)
#define STM32_USB_CNTR_ESOFM	    BIT(8)
#define STM32_USB_CNTR_SOFM	    BIT(9)
#define STM32_USB_CNTR_RESETM	    BIT(10)
#define STM32_USB_CNTR_SUSPM	    BIT(11)
#define STM32_USB_CNTR_WKUPM	    BIT(12)
#define STM32_USB_CNTR_ERRM	    BIT(13)
#define STM32_USB_CNTR_PMAOVRM	    BIT(14)
#define STM32_USB_CNTR_CTRM	    BIT(15)

#define STM32_USB_ISTR             REG16(STM32_USB_FS_BASE + 0x44)

#define STM32_USB_ISTR_EP_ID_MASK   (0x000f)
#define STM32_USB_ISTR_DIR	    BIT(4)
#define STM32_USB_ISTR_L1REQ	    BIT(7)
#define STM32_USB_ISTR_ESOF	    BIT(8)
#define STM32_USB_ISTR_SOF	    BIT(9)
#define STM32_USB_ISTR_RESET	    BIT(10)
#define STM32_USB_ISTR_SUSP	    BIT(11)
#define STM32_USB_ISTR_WKUP	    BIT(12)
#define STM32_USB_ISTR_ERR	    BIT(13)
#define STM32_USB_ISTR_PMAOVR	    BIT(14)
#define STM32_USB_ISTR_CTR	    BIT(15)

#define STM32_USB_FNR              REG16(STM32_USB_FS_BASE + 0x48)

#define STM32_USB_FNR_RXDP_RXDM_SHIFT (14)
#define STM32_USB_FNR_RXDP_RXDM_MASK  (3 << STM32_USB_FNR_RXDP_RXDM_SHIFT)

#define STM32_USB_DADDR            REG16(STM32_USB_FS_BASE + 0x4C)
#define STM32_USB_BTABLE           REG16(STM32_USB_FS_BASE + 0x50)
#define STM32_USB_LPMCSR           REG16(STM32_USB_FS_BASE + 0x54)
#define STM32_USB_BCDR             REG16(STM32_USB_FS_BASE + 0x58)

#define STM32_USB_BCDR_BCDEN	    BIT(0)
#define STM32_USB_BCDR_DCDEN	    BIT(1)
#define STM32_USB_BCDR_PDEN	    BIT(2)
#define STM32_USB_BCDR_SDEN	    BIT(3)
#define STM32_USB_BCDR_DCDET	    BIT(4)
#define STM32_USB_BCDR_PDET	    BIT(5)
#define STM32_USB_BCDR_SDET	    BIT(6)
#define STM32_USB_BCDR_PS2DET	    BIT(7)

#define EP_MASK     0x0F0F
#define EP_TX_DTOG  0x0040
#define EP_TX_MASK  0x0030
#define EP_TX_VALID 0x0030
#define EP_TX_NAK   0x0020
#define EP_TX_STALL 0x0010
#define EP_TX_DISAB 0x0000
#define EP_RX_DTOG  0x4000
#define EP_RX_MASK  0x3000
#define EP_RX_VALID 0x3000
#define EP_RX_NAK   0x2000
#define EP_RX_STALL 0x1000
#define EP_RX_DISAB 0x0000

#define EP_STATUS_OUT 0x0100

#define EP_TX_RX_MASK (EP_TX_MASK | EP_RX_MASK)
#define EP_TX_RX_VALID (EP_TX_VALID | EP_RX_VALID)

#define STM32_TOGGLE_EP(n, mask, val, flags) \
	STM32_USB_EP(n) = (((STM32_USB_EP(n) & (EP_MASK | (mask))) \
			^ (val)) | (flags))

/* --- TRNG --- */
#define STM32_RNG_CR                REG32(STM32_RNG_BASE + 0x0)
#define STM32_RNG_CR_RNGEN          BIT(2)
#define STM32_RNG_CR_IE             BIT(3)
#define STM32_RNG_CR_CED            BIT(5)
#define STM32_RNG_SR                REG32(STM32_RNG_BASE + 0x4)
#define STM32_RNG_SR_DRDY           BIT(0)
#define STM32_RNG_DR                REG32(STM32_RNG_BASE + 0x8)

/* --- AXI interconnect --- */

/* STM32H7: AXI_TARGx_FN_MOD exists for masters x = 1, 2 and 7 */
#define STM32_AXI_TARG_FN_MOD(x)    REG32(STM32_GPV_BASE + 0x1108 + \
					  0x1000 * (x))
#define  WRITE_ISS_OVERRIDE         BIT(1)
#define  READ_ISS_OVERRIDE          BIT(0)

/* --- MISC --- */
#define STM32_UNIQUE_ID_ADDRESS     REG32_ADDR(STM32_UNIQUE_ID_BASE)
#define STM32_UNIQUE_ID_LENGTH      (3 * 4)

#endif /* !__ASSEMBLER__ */
