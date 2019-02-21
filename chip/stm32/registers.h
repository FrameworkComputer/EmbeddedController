/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for STM32 processor
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"
#include "compile_time_macros.h"

/* IRQ numbers */
#ifdef CHIP_FAMILY_STM32F0
#define STM32_IRQ_WWDG             0
#define STM32_IRQ_PVD              1
#define STM32_IRQ_RTC_WAKEUP       2
#define STM32_IRQ_RTC_ALARM        2
#define STM32_IRQ_FLASH            3
#define STM32_IRQ_RCC              4
#define STM32_IRQ_EXTI0_1          5
#define STM32_IRQ_EXTI2_3          6
#define STM32_IRQ_EXTI4_15         7
#define STM32_IRQ_TSC              8
#define STM32_IRQ_DMA_CHANNEL_1    9
#define STM32_IRQ_DMA_CHANNEL_2_3 10
#define STM32_IRQ_DMA_CHANNEL_4_7 11
#define STM32_IRQ_ADC_COMP        12
#define STM32_IRQ_TIM1_BRK_UP_TRG 13
#define STM32_IRQ_TIM1_CC         14
#define STM32_IRQ_TIM2            15
#define STM32_IRQ_TIM3            16
#define STM32_IRQ_TIM6_DAC        17
#define STM32_IRQ_TIM7            18
#define STM32_IRQ_TIM14           19
#define STM32_IRQ_TIM15           20
#define STM32_IRQ_TIM16           21
#define STM32_IRQ_TIM17           22
#define STM32_IRQ_I2C1            23
#define STM32_IRQ_I2C2            24
#define STM32_IRQ_SPI1            25
#define STM32_IRQ_SPI2            26
#define STM32_IRQ_USART1          27
#define STM32_IRQ_USART2          28
#define STM32_IRQ_USART3_4        29
#define STM32_IRQ_CEC_CAN         30
#define STM32_IRQ_USB             31
/* aliases for easier code sharing */
#define STM32_IRQ_COMP STM32_IRQ_ADC_COMP
#define STM32_IRQ_USB_LP STM32_IRQ_USB

#else /* !CHIP_FAMILY_STM32F0 */
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

#ifdef CHIP_VARIANT_STM32F373
#define STM32_IRQ_USB_HP          74
#define STM32_IRQ_USB_LP          75
#else
#define STM32_IRQ_USB_HP          19
#define STM32_IRQ_USB_LP          20
#endif

#define STM32_IRQ_ADC1            18 /* STM32L4 only */
#define STM32_IRQ_CAN_TX          19 /* STM32F373 only */
#define STM32_IRQ_USB_LP_CAN_RX   20 /* STM32F373 only */
#define STM32_IRQ_DAC             21
#define STM32_IRQ_CAN_RX1         21 /* STM32F373 only */

#ifdef CHIP_VARIANT_STM32F373
#define STM32_IRQ_COMP            64
#else
#define STM32_IRQ_COMP            22
#endif

#define STM32_IRQ_CAN_SCE         22 /* STM32F373 only */
#define STM32_IRQ_EXTI9_5         23
#ifndef CHIP_FAMILY_STM32H7
#define STM32_IRQ_LCD             24 /* STM32L15X only */
#define STM32_IRQ_TIM15           24 /* STM32F373 only */
#define STM32_IRQ_TIM9            25 /* STM32L15X only */
#define STM32_IRQ_TIM16           25 /* STM32F373 only */
#define STM32_IRQ_TIM10           26 /* STM32L15X only */
#define STM32_IRQ_TIM17           26 /* STM32F373 only */
#define STM32_IRQ_TIM11           27 /* STM32L15X only */
#define STM32_IRQ_TIM18_DAC2      27 /* STM32F373 only */
#endif /* !CHIP_FAMILY_STM32H7 */
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

#ifdef CHIP_FAMILY_STM32H7
#define STM32_IRQ_LPTIM1         93
#define STM32_IRQ_TIM15         116
#define STM32_IRQ_TIM16         117
#define STM32_IRQ_TIM17         118
#define STM32_IRQ_LPTIM2        138
#define STM32_IRQ_LPTIM3        139
#define STM32_IRQ_LPTIM4        140
#define STM32_IRQ_LPTIM5        141
#endif /* CHIP_FAMILY_STM32H7 */

/* To simplify code generation, define DMA channel 9..10 */
#define STM32_IRQ_DMA_CHANNEL_9    STM32_IRQ_DMA2_CHANNEL1
#define STM32_IRQ_DMA_CHANNEL_10   STM32_IRQ_DMA2_CHANNEL2
#define STM32_IRQ_DMA_CHANNEL_13   STM32_IRQ_DMA2_CHANNEL6
#define STM32_IRQ_DMA_CHANNEL_14   STM32_IRQ_DMA2_CHANNEL7

/* aliases for easier code sharing */
#define STM32_IRQ_I2C1 STM32_IRQ_I2C1_EV
#define STM32_IRQ_I2C2 STM32_IRQ_I2C2_EV
#define STM32_IRQ_I2C3 STM32_IRQ_I2C3_EV
#endif /* !CHIP_FAMILY_STM32F0 */

#if defined(CHIP_VARIANT_STM32F411) || defined(CHIP_VARIANT_STM32F412)
#define CHIP_VARIANT_STM32F41X
#endif

#if defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32H7)
/*
 * STM32F4 introduces a concept of DMA stream to allow
 * fine allocation of a stream to a channel.
 */
#define STM32_IRQ_DMA1_STREAM0    11
#define STM32_IRQ_DMA1_STREAM1    12
#define STM32_IRQ_DMA1_STREAM2    13
#define STM32_IRQ_DMA1_STREAM3    14
#define STM32_IRQ_DMA1_STREAM4    15
#define STM32_IRQ_DMA1_STREAM5    16
#define STM32_IRQ_DMA1_STREAM6    17
#define STM32_IRQ_DMA1_STREAM7    47
#define STM32_IRQ_DMA2_STREAM0    56
#define STM32_IRQ_DMA2_STREAM1    57
#define STM32_IRQ_DMA2_STREAM2    58
#define STM32_IRQ_DMA2_STREAM3    59
#define STM32_IRQ_DMA2_STREAM4    60
#define STM32_IRQ_DMA2_STREAM5    68
#define STM32_IRQ_DMA2_STREAM6    69
#define STM32_IRQ_DMA2_STREAM7    70

#define STM32_IRQ_OTG_HS_WKUP     76
#define STM32_IRQ_OTG_HS_EP1_IN   75
#define STM32_IRQ_OTG_HS_EP1_OUT  74
#define STM32_IRQ_OTG_HS          77
#define STM32_IRQ_OTG_FS          67
#define STM32_IRQ_OTG_FS_WKUP     42

#endif

/* Peripheral base addresses */

#ifndef CHIP_FAMILY_STM32H7
#if defined(CHIP_FAMILY_STM32F4)
#define STM32_ADC1_BASE             0x40012000
#define STM32_ADC_BASE              0x40012300
#else
#define STM32_ADC1_BASE             0x40012400
#define STM32_ADC_BASE              0x40012700 /* STM32L15X only */
#endif

#define STM32_CEC_BASE              0x40007800 /* STM32F373 */
#define STM32_CRC_BASE              0x40023000
#define STM32_CRS_BASE              0x40006c00 /* STM32F0XX */
#define STM32_DAC_BASE              0x40007400

#if defined(CHIP_FAMILY_STM32L)
#define STM32_COMP_BASE             0x40007C00
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
#define STM32_COMP_BASE             0x40010000
#endif

#ifdef CHIP_FAMILY_STM32F0
#define STM32_DBGMCU_BASE           0x40015800
#else
#define STM32_DBGMCU_BASE           0xE0042000
#endif

#if defined(CHIP_FAMILY_STM32L)
#define STM32_DMA1_BASE             0x40026000
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32L4)
#define STM32_DMA1_BASE             0x40020000
#define STM32_DMA2_BASE             0x40020400
#elif defined(CHIP_FAMILY_STM32F4)
#define STM32_DMA1_BASE             0x40026000
#define STM32_DMA2_BASE             0x40026400
#endif

#if defined(CHIP_FAMILY_STM32F4)
#define STM32_EXTI_BASE             0x40013C00
#else
#define STM32_EXTI_BASE             0x40010400
#endif

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32F4)
#define STM32_FLASH_REGS_BASE       0x40023c00
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32L4)
#define STM32_FLASH_REGS_BASE       0x40022000
#endif

#if defined(CHIP_FAMILY_STM32L)
#define STM32_GPIOA_BASE            0x40020000
#define STM32_GPIOB_BASE            0x40020400
#define STM32_GPIOC_BASE            0x40020800
#define STM32_GPIOD_BASE            0x40020C00
#define STM32_GPIOE_BASE            0x40021000
#define STM32_GPIOF_BASE            0x40021800 /* see RM0038 table 5 */
#define STM32_GPIOG_BASE            0x40021C00
#define STM32_GPIOH_BASE            0x40021400
#elif defined(CHIP_FAMILY_STM32F4)
#define STM32_GPIOA_BASE            0x40020000
#define STM32_GPIOB_BASE            0x40020400
#define STM32_GPIOC_BASE            0x40020800
#define STM32_GPIOD_BASE            0x40020C00
#define STM32_GPIOE_BASE            0x40021000
#define STM32_GPIOF_BASE            0x40021400 /* see RM0402/0390 table 1 */
#define STM32_GPIOG_BASE            0x40021800
#define STM32_GPIOH_BASE            0x40021C00
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32L4)
#define STM32_GPIOA_BASE            0x48000000
#define STM32_GPIOB_BASE            0x48000400
#define STM32_GPIOC_BASE            0x48000800
#define STM32_GPIOD_BASE            0x48000C00
#define STM32_GPIOE_BASE            0x48001000
#define STM32_GPIOF_BASE            0x48001400
#define STM32_GPIOG_BASE            0x48001800 /* only for stm32l4x6 */
#define STM32_GPIOH_BASE            0x48001C00 /* only for stm32l4 */
#endif

#define STM32_I2C1_BASE             0x40005400
#define STM32_I2C2_BASE             0x40005800
#define STM32_I2C3_BASE             0x40005C00
#define STM32_I2C4_BASE             0x40006000

#define STM32_IWDG_BASE             0x40003000
#define STM32_LCD_BASE              0x40002400

#if defined(CHIP_FAMILY_STM32L)
#define STM32_OPTB_BASE             0x1ff80000
#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
#define STM32_OPTB_BASE             0x1FFFF800
#elif defined(CHIP_FAMILY_STM32L4)
#define STM32_OPTB_BASE             0x1FFF7800
#elif defined(CHIP_FAMILY_STM32F4)
#define STM32_OPTB_BASE             0x1FFFC000
#define STM32_OTP_BASE              0x1FFF7800
#endif

#define STM32_PMSE_BASE             0x40013400
#define STM32_PWR_BASE              0x40007000

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32F4)
#define STM32_RCC_BASE              0x40023800
#elif defined(CHIP_FAMILY_STM32L4) || defined(CHIP_FAMILY_STM32F0) || \
	defined(CHIP_FAMILY_STM32F3)
#define STM32_RCC_BASE              0x40021000
#endif

#define STM32_RI_BASE               0x40007C00 /* STM32L1xx only */
#define STM32_RNG_BASE              0x50060800 /* STM32L4 */
#define STM32_RTC_BASE              0x40002800

#define STM32_SPI1_BASE             0x40013000
#define STM32_SPI2_BASE             0x40003800
#define STM32_SPI3_BASE             0x40003c00 /* STM32F373, STM32L4, STM32F7 */

#ifdef CHIP_FAMILY_STM32F4
#define STM32_SYSCFG_BASE           0x40013800
#else
#define STM32_SYSCFG_BASE           0x40010000
#endif

#define STM32_TIM1_BASE             0x40012c00 /* STM32F373 */
#define STM32_TIM2_BASE             0x40000000
#define STM32_TIM3_BASE             0x40000400
#define STM32_TIM4_BASE             0x40000800
#define STM32_TIM5_BASE             0x40000c00 /* STM32F373 */
#define STM32_TIM6_BASE             0x40001000
#define STM32_TIM7_BASE             0x40001400
#if defined(CHIP_FAMILY_STM32L)
#define STM32_TIM9_BASE             0x40010800 /* STM32L15X only */
#define STM32_TIM10_BASE            0x40010C00 /* STM32L15X only */
#define STM32_TIM11_BASE            0x40011000 /* STM32L15X only */
#elif defined(CHIP_FAMILY_STM32F4)
#define STM32_TIM9_BASE             0x40014000 /* STM32F41x only */
#define STM32_TIM10_BASE            0x40014400 /* STM32F41x only */
#define STM32_TIM11_BASE            0x40014800 /* STM32F41x only */
#endif /* TIM9-11 */
#define STM32_TIM12_BASE            0x40001800 /* STM32F373 */
#define STM32_TIM13_BASE            0x40001c00 /* STM32F373 */
#define STM32_TIM14_BASE            0x40002000 /* STM32F373 */
#define STM32_TIM15_BASE            0x40014000
#define STM32_TIM16_BASE            0x40014400
#define STM32_TIM17_BASE            0x40014800
#define STM32_TIM18_BASE            0x40009c00 /* STM32F373 only */
#define STM32_TIM19_BASE            0x40015c00 /* STM32F373 only */

#ifdef CHIP_FAMILY_STM32F4
#define STM32_UNIQUE_ID_BASE        0x1fff7a10
#else
#define STM32_UNIQUE_ID_BASE        0x1ffff7ac
#endif

#if defined(CHIP_FAMILY_STM32F4)
#define STM32_USART1_BASE           0x40011000
#define STM32_USART2_BASE           0x40004400
#define STM32_USART3_BASE           0x40004800
#define STM32_USART4_BASE           0x40004c00
#define STM32_USART5_BASE           0x40005000
#define STM32_USART6_BASE           0x40011400
#else
#define STM32_USART1_BASE           0x40013800
#define STM32_USART2_BASE           0x40004400
#define STM32_USART3_BASE           0x40004800
#define STM32_USART4_BASE           0x40004c00
#define STM32_USART9_BASE           0x40008000 /* LPUART */
#endif

#define STM32_USB_CAN_SRAM_BASE     0x40006000
#define STM32_USB_FS_BASE           0x40005C00

#define STM32_WWDG_BASE             0x40002C00

#else  /* CHIP_FAMILY_STM32H7 */

#define STM32_GPV_BASE              0x51000000

#define STM32_DBGMCU_BASE           0x5C001000

#define STM32_BDMA_BASE             0x58025400
#define STM32_DMA1_BASE             0x40020000
#define STM32_DMA2_BASE             0x40020400
#define STM32_DMA2D_BASE            0x52001000
#define STM32_DMAMUX1_BASE          0x40020800
#define STM32_DMAMUX2_BASE          0x58025800
#define STM32_MDMA_BASE             0x52000000

#define STM32_EXTI_BASE             0x58000000

#define STM32_FLASH_REGS_BASE       0x52002000

#define STM32_GPIOA_BASE            0x58020000
#define STM32_GPIOB_BASE            0x58020400
#define STM32_GPIOC_BASE            0x58020800
#define STM32_GPIOD_BASE            0x58020C00
#define STM32_GPIOE_BASE            0x58021000
#define STM32_GPIOF_BASE            0x58021400
#define STM32_GPIOG_BASE            0x58021800
#define STM32_GPIOH_BASE            0x58021C00
#define STM32_GPIOI_BASE            0x58022000
#define STM32_GPIOJ_BASE            0x58022400
#define STM32_GPIOK_BASE            0x58022800

#define STM32_IWDG_BASE             0x58004800

#define STM32_LPTIM1_BASE           0x40002400
#define STM32_LPTIM2_BASE           0x58002400
#define STM32_LPTIM3_BASE           0x58002800
#define STM32_LPTIM4_BASE           0x58002C00
#define STM32_LPTIM5_BASE           0x58003000

#define STM32_PWR_BASE              0x58024800
#define STM32_RCC_BASE              0x58024400
#define STM32_RNG_BASE              0x48021800
#define STM32_RTC_BASE              0x58004000

#define STM32_SYSCFG_BASE           0x58000400

#define STM32_SPI1_BASE             0x40013000
#define STM32_SPI2_BASE             0x40003800
#define STM32_SPI3_BASE             0x40003c00
#define STM32_SPI4_BASE             0x40013400
#define STM32_SPI5_BASE             0x40015000

#define STM32_TIM1_BASE             0x40010000
#define STM32_TIM2_BASE             0x40000000
#define STM32_TIM3_BASE             0x40000400
#define STM32_TIM4_BASE             0x40000800
#define STM32_TIM5_BASE             0x40000c00
#define STM32_TIM6_BASE             0x40001000
#define STM32_TIM7_BASE             0x40001400
#define STM32_TIM8_BASE             0x40010400
#define STM32_TIM12_BASE            0x40001800
#define STM32_TIM13_BASE            0x40001c00
#define STM32_TIM14_BASE            0x40002000
#define STM32_TIM15_BASE            0x40014000
#define STM32_TIM16_BASE            0x40014400
#define STM32_TIM17_BASE            0x40014800

#define STM32_UNIQUE_ID_BASE        0x1ff1e800

#define STM32_USART1_BASE           0x40011000
#define STM32_USART2_BASE           0x40004400
#define STM32_USART3_BASE           0x40004800
#define STM32_USART4_BASE           0x40004c00
#define STM32_USART5_BASE           0x40005000
#define STM32_USART6_BASE           0x40011400
#define STM32_USART7_BASE           0x40007800
#define STM32_USART8_BASE           0x40007C00

#endif /* CHIP_FAMILY_STM32H7 */

#ifndef __ASSEMBLER__

/* Register definitions */

/* --- USART --- */
#define STM32_USART_BASE(n)           CONCAT3(STM32_USART, n, _BASE)
#define STM32_USART_REG(base, offset) REG32((base) + (offset))

#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32L4) || defined(CHIP_VARIANT_STM32F76X) || \
	defined(CHIP_FAMILY_STM32H7)
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
#else
/* !CHIP_FAMILY_STM32F0 && !CHIP_FAMILY_STM32F3 && !CHIP_FAMILY_STM32L4 */
#define STM32_USART_SR(base)       STM32_USART_REG(base, 0x00)
#define STM32_USART_SR_ORE		BIT(3)
#define STM32_USART_SR_RXNE		BIT(5)
#define STM32_USART_SR_TC		BIT(6)
#define STM32_USART_SR_TXE		BIT(7)
#define STM32_USART_DR(base)       STM32_USART_REG(base, 0x04)
#define STM32_USART_BRR(base)      STM32_USART_REG(base, 0x08)
#define STM32_USART_CR1(base)      STM32_USART_REG(base, 0x0C)
#define STM32_USART_CR1_RE		BIT(2)
#define STM32_USART_CR1_TE		BIT(3)
#define STM32_USART_CR1_RXNEIE		BIT(5)
#define STM32_USART_CR1_TCIE		BIT(6)
#define STM32_USART_CR1_TXEIE		BIT(7)
#define STM32_USART_CR1_PS		BIT(9)
#define STM32_USART_CR1_PCE		BIT(10)
#define STM32_USART_CR1_M		BIT(12)
#define STM32_USART_CR1_UE		BIT(13)
#define STM32_USART_CR1_OVER8		BIT(15) /* STM32L only */
#define STM32_USART_CR2(base)      STM32_USART_REG(base, 0x10)
#define STM32_USART_CR3(base)      STM32_USART_REG(base, 0x14)
#define STM32_USART_CR3_EIE		BIT(0)
#define STM32_USART_CR3_DMAR		BIT(6)
#define STM32_USART_CR3_DMAT		BIT(7)
#define STM32_USART_CR3_ONEBIT		BIT(11) /* STM32L only */
#define STM32_USART_GTPR(base)     STM32_USART_REG(base, 0x18)
/* register aliases */
#define STM32_USART_TDR(base)      STM32_USART_DR(base)
#define STM32_USART_RDR(base)      STM32_USART_DR(base)
#endif
/* !CHIP_FAMILY_STM32F0 && !CHIP_FAMILY_STM32F3 && !CHIP_FAMILY_STM32L4 */

#define STM32_IRQ_USART(n)         CONCAT2(STM32_IRQ_USART, n)

/* --- TIMERS --- */
#define STM32_TIM_BASE(n)          CONCAT3(STM32_TIM, n, _BASE)

#define STM32_TIM_REG(n, offset) \
		REG16(STM32_TIM_BASE(n) + (offset))
#define STM32_TIM_REG32(n, offset) \
		REG32(STM32_TIM_BASE(n) + (offset))

#define STM32_TIM_CR1(n)           STM32_TIM_REG(n, 0x00)
#define STM32_TIM_CR1_CEN          BIT(0)
#define STM32_TIM_CR2(n)           STM32_TIM_REG(n, 0x04)
#define STM32_TIM_SMCR(n)          STM32_TIM_REG(n, 0x08)
#define STM32_TIM_DIER(n)          STM32_TIM_REG(n, 0x0C)
#define STM32_TIM_SR(n)            STM32_TIM_REG(n, 0x10)
#define STM32_TIM_EGR(n)           STM32_TIM_REG(n, 0x14)
#define STM32_TIM_EGR_UG           BIT(0)
#define STM32_TIM_CCMR1(n)         STM32_TIM_REG(n, 0x18)
#define STM32_TIM_CCMR1_OC1PE      BIT(2)
/* Use in place of TIM_CCMR1_OC1M_0 through 2 from STM documentation. */
#define STM32_TIM_CCMR1_OC1M(n)    (((n) & 0x7) << 4)
#define STM32_TIM_CCMR1_OC1M_MASK              STM32_TIM_CCMR1_OC1M(~0)
#define STM32_TIM_CCMR1_OC1M_FROZEN            STM32_TIM_CCMR1_OC1M(0x0)
#define STM32_TIM_CCMR1_OC1M_ACTIVE_ON_MATCH   STM32_TIM_CCMR1_OC1M(0x1)
#define STM32_TIM_CCMR1_OC1M_INACTIVE_ON_MATCH STM32_TIM_CCMR1_OC1M(0x2)
#define STM32_TIM_CCMR1_OC1M_TOGGLE            STM32_TIM_CCMR1_OC1M(0x3)
#define STM32_TIM_CCMR1_OC1M_FORCE_INACTIVE    STM32_TIM_CCMR1_OC1M(0x4)
#define STM32_TIM_CCMR1_OC1M_FORCE_ACTIVE      STM32_TIM_CCMR1_OC1M(0x5)
#define STM32_TIM_CCMR1_OC1M_PWM_MODE_1        STM32_TIM_CCMR1_OC1M(0x6)
#define STM32_TIM_CCMR1_OC1M_PWM_MODE_2        STM32_TIM_CCMR1_OC1M(0x7)
#define STM32_TIM_CCMR2(n)         STM32_TIM_REG(n, 0x1C)
#define STM32_TIM_CCER(n)          STM32_TIM_REG(n, 0x20)
#define STM32_TIM_CCER_CC1E        BIT(0)
#define STM32_TIM_CNT(n)           STM32_TIM_REG(n, 0x24)
#define STM32_TIM_PSC(n)           STM32_TIM_REG(n, 0x28)
#define STM32_TIM_ARR(n)           STM32_TIM_REG(n, 0x2C)
#define STM32_TIM_RCR(n)           STM32_TIM_REG(n, 0x30)
#define STM32_TIM_CCR1(n)          STM32_TIM_REG(n, 0x34)
#define STM32_TIM_CCR2(n)          STM32_TIM_REG(n, 0x38)
#define STM32_TIM_CCR3(n)          STM32_TIM_REG(n, 0x3C)
#define STM32_TIM_CCR4(n)          STM32_TIM_REG(n, 0x40)
#define STM32_TIM_BDTR(n)          STM32_TIM_REG(n, 0x44)
#define STM32_TIM_BDTR_MOE         BIT(15)
#define STM32_TIM_DCR(n)           STM32_TIM_REG(n, 0x48)
#define STM32_TIM_DMAR(n)          STM32_TIM_REG(n, 0x4C)
#define STM32_TIM_OR(n)            STM32_TIM_REG(n, 0x50)

#define STM32_TIM_CCRx(n, x)       STM32_TIM_REG(n, 0x34 + ((x) - 1) * 4)

#define STM32_TIM32_CNT(n)         STM32_TIM_REG32(n, 0x24)
#define STM32_TIM32_ARR(n)         STM32_TIM_REG32(n, 0x2C)
#define STM32_TIM32_CCR1(n)        STM32_TIM_REG32(n, 0x34)
#define STM32_TIM32_CCR2(n)        STM32_TIM_REG32(n, 0x38)
#define STM32_TIM32_CCR3(n)        STM32_TIM_REG32(n, 0x3C)
#define STM32_TIM32_CCR4(n)        STM32_TIM_REG32(n, 0x40)
/* Timer registers as struct */
struct timer_ctlr {
	unsigned cr1;
	unsigned cr2;
	unsigned smcr;
	unsigned dier;

	unsigned sr;
	unsigned egr;
	unsigned ccmr1;
	unsigned ccmr2;

	unsigned ccer;
	unsigned cnt;
	unsigned psc;
	unsigned arr;

	unsigned ccr[5]; /* ccr[0] = reserved30 */

	unsigned bdtr;
	unsigned dcr;
	unsigned dmar;

	unsigned or;
};
/* Must be volatile, or compiler optimizes out repeated accesses */
typedef volatile struct timer_ctlr timer_ctlr_t;

/* --- Low power timers --- */
#define STM32_LPTIM_BASE(n)          CONCAT3(STM32_LPTIM, n, _BASE)

#define STM32_LPTIM_REG(n, offset)   REG32(STM32_LPTIM_BASE(n) + (offset))

#define STM32_LPTIM_ISR(n)           STM32_LPTIM_REG(n, 0x00)
#define STM32_LPTIM_ICR(n)           STM32_LPTIM_REG(n, 0x04)
#define STM32_LPTIM_IER(n)           STM32_LPTIM_REG(n, 0x08)
#define STM32_LPTIM_INT_DOWN         BIT(6)
#define STM32_LPTIM_INT_UP           BIT(5)
#define STM32_LPTIM_INT_ARROK        BIT(4)
#define STM32_LPTIM_INT_CMPOK        BIT(3)
#define STM32_LPTIM_INT_EXTTRIG      BIT(2)
#define STM32_LPTIM_INT_ARRM         BIT(1)
#define STM32_LPTIM_INT_CMPM         BIT(0)
#define STM32_LPTIM_CFGR(n)          STM32_LPTIM_REG(n, 0x0C)
#define STM32_LPTIM_CR(n)            STM32_LPTIM_REG(n, 0x10)
#define STM32_LPTIM_CR_RSTARE        BIT(4)
#define STM32_LPTIM_CR_COUNTRST      BIT(3)
#define STM32_LPTIM_CR_CNTSTRT       BIT(2)
#define STM32_LPTIM_CR_SNGSTRT       BIT(1)
#define STM32_LPTIM_CR_ENABLE        BIT(0)
#define STM32_LPTIM_CMP(n)           STM32_LPTIM_REG(n, 0x14)
#define STM32_LPTIM_ARR(n)           STM32_LPTIM_REG(n, 0x18)
#define STM32_LPTIM_CNT(n)           STM32_LPTIM_REG(n, 0x1C)
#define STM32_LPTIM_CFGR2(n)         STM32_LPTIM_REG(n, 0x24)

/* --- GPIO --- */

#define GPIO_A                       STM32_GPIOA_BASE
#define GPIO_B                       STM32_GPIOB_BASE
#define GPIO_C                       STM32_GPIOC_BASE
#define GPIO_D                       STM32_GPIOD_BASE
#define GPIO_E                       STM32_GPIOE_BASE
#define GPIO_F                       STM32_GPIOF_BASE
#define GPIO_G                       STM32_GPIOG_BASE
#define GPIO_H                       STM32_GPIOH_BASE
#define GPIO_I                       STM32_GPIOI_BASE
#define GPIO_J                       STM32_GPIOJ_BASE
#define GPIO_K                       STM32_GPIOK_BASE

#define DUMMY_GPIO_BANK GPIO_A

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32F4) || \
	defined(CHIP_FAMILY_STM32H7)

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

#define GPIO_ALT_SYS                 0x0
#define GPIO_ALT_TIM2                0x1
#define GPIO_ALT_TIM3_4              0x2
#define GPIO_ALT_TIM9_11             0x3
#define GPIO_ALT_I2C                 0x4
#define GPIO_ALT_SPI                 0x5
#define GPIO_ALT_SPI3                0x6
#define GPIO_ALT_USART               0x7
#define GPIO_ALT_I2C_23              0x9
#define GPIO_ALT_USB                 0xA
#define GPIO_ALT_LCD                 0xB
#define GPIO_ALT_RI                  0xE
#define GPIO_ALT_EVENTOUT            0xF

#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32L4)

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
#else
#error Unsupported chip variant
#endif

/* --- I2C --- */
#define STM32_I2C1_PORT             0
#define STM32_I2C2_PORT             1
#define STM32_I2C3_PORT             2
#define STM32_FMPI2C4_PORT          3

#define stm32_i2c_reg(port, offset) \
	((uint16_t *)((STM32_I2C1_BASE + ((port) * 0x400)) + (offset)))

#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) \
	|| defined(CHIP_FAMILY_STM32L4)
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
#else /* !CHIP_FAMILY_STM32F0 && !CHIP_FAMILY_STM32F3 */
#define STM32_I2C_CR1(n)            REG16(stm32_i2c_reg(n, 0x00))
#define STM32_I2C_CR1_PE	BIT(0)
#define STM32_I2C_CR1_START	BIT(8)
#define STM32_I2C_CR1_STOP	BIT(9)
#define STM32_I2C_CR1_ACK	BIT(10)
#define STM32_I2C_CR1_POS	BIT(11)
#define STM32_I2C_CR1_SWRST	BIT(15)
#define STM32_I2C_CR2(n)            REG16(stm32_i2c_reg(n, 0x04))
#define STM32_I2C_CR2_ITERREN	BIT(8)
#define STM32_I2C_CR2_ITEVTEN	BIT(9)
#define STM32_I2C_CR2_ITBUFEN	BIT(10)
#define STM32_I2C_CR2_DMAEN	BIT(11)
#define STM32_I2C_CR2_LAST	BIT(12)
#define STM32_I2C_OAR1(n)           REG16(stm32_i2c_reg(n, 0x08))
#define STM32_I2C_OAR1_B14	BIT(14)
#define STM32_I2C_OAR2(n)           REG16(stm32_i2c_reg(n, 0x0C))
#define STM32_I2C_OAR2_ENDUAL	BIT(0)
#define STM32_I2C_DR(n)             REG16(stm32_i2c_reg(n, 0x10))
#define STM32_I2C_SR1(n)            REG16(stm32_i2c_reg(n, 0x14))
#define STM32_I2C_SR1_SB	BIT(0)
#define STM32_I2C_SR1_ADDR	BIT(1)
#define STM32_I2C_SR1_BTF	BIT(2)
#define STM32_I2C_SR1_STOPF	BIT(4)
#define STM32_I2C_SR1_RXNE	BIT(6)
#define STM32_I2C_SR1_TXE	BIT(7)
#define STM32_I2C_SR1_BERR	BIT(8)
#define STM32_I2C_SR1_ARLO	BIT(9)
#define STM32_I2C_SR1_AF	BIT(10)

#define STM32_I2C_SR2(n)            REG16(stm32_i2c_reg(n, 0x18))
#define STM32_I2C_SR2_BUSY	BIT(1)
#define STM32_I2C_SR2_TRA	BIT(2)
#define STM32_I2C_SR2_DUALF	BIT(7)

#define STM32_I2C_CCR(n)            REG16(stm32_i2c_reg(n, 0x1C))
#define STM32_I2C_CCR_DUTY	BIT(14)
#define STM32_I2C_CCR_FM	BIT(15)
#define STM32_I2C_TRISE(n)          REG16(stm32_i2c_reg(n, 0x20))
/* !CHIP_FAMILY_STM32F0 && !CHIP_FAMILY_STM32F3 && !CHIP_FAMILY_STM32L4 */
#endif



#if defined(CHIP_FAMILY_STM32F4)
#define STM32_FMPI2C_CR1(n)        REG32(stm32_i2c_reg(n, 0x00))
#define  FMPI2C_CR1_PE             BIT(0)
#define  FMPI2C_CR1_TXDMAEN        BIT(14)
#define  FMPI2C_CR1_RXDMAEN        BIT(15)
#define STM32_FMPI2C_CR2(n)        REG32(stm32_i2c_reg(n, 0x04))
#define  FMPI2C_CR2_RD_WRN         BIT(10)
#define  FMPI2C_READ               1
#define  FMPI2C_WRITE              0
#define  FMPI2C_CR2_START          BIT(13)
#define  FMPI2C_CR2_STOP           BIT(14)
#define  FMPI2C_CR2_NACK           BIT(15)
#define  FMPI2C_CR2_RELOAD         BIT(24)
#define  FMPI2C_CR2_AUTOEND        BIT(25)
#define  FMPI2C_CR2_SADD(addr)     ((addr) & 0x3ff)
#define  FMPI2C_CR2_SADD_MASK      FMPI2C_CR2_SADD(0x3ff)
#define  FMPI2C_CR2_SIZE(size)     (((size) & 0xff) << 16)
#define  FMPI2C_CR2_SIZE_MASK      FMPI2C_CR2_SIZE(0xf)
#define STM32_FMPI2C_OAR1(n)       REG32(stm32_i2c_reg(n, 0x08))
#define STM32_FMPI2C_OAR2(n)       REG32(stm32_i2c_reg(n, 0x0C))
#define STM32_FMPI2C_TIMINGR(n)    REG32(stm32_i2c_reg(n, 0x10))
#define  TIMINGR_THE_RIGHT_VALUE   0xC0000E12
#define  FMPI2C_TIMINGR_PRESC(val) (((val) & 0xf) << 28)
#define  FMPI2C_TIMINGR_SCLDEL(val) (((val) & 0xf) << 20)
#define  FMPI2C_TIMINGR_SDADEL(val) (((val) & 0xf) << 16)
#define  FMPI2C_TIMINGR_SCLH(val)  (((val) & 0xff) << 8)
#define  FMPI2C_TIMINGR_SCLL(val)  (((val) & 0xff) << 0)
#define STM32_FMPI2C_TIMEOUTR(n)   REG32(stm32_i2c_reg(n, 0x14))

#define STM32_FMPI2C_ISR(n)        REG32(stm32_i2c_reg(n, 0x18))
#define  FMPI2C_ISR_TXE            BIT(0)
#define  FMPI2C_ISR_TXIS           BIT(1)
#define  FMPI2C_ISR_RXNE           BIT(2)
#define  FMPI2C_ISR_ADDR           BIT(3)
#define  FMPI2C_ISR_NACKF          BIT(4)
#define  FMPI2C_ISR_STOPF          BIT(5)
#define  FMPI2C_ISR_BERR           BIT(8)
#define  FMPI2C_ISR_ARLO           BIT(9)
#define  FMPI2C_ISR_BUSY           BIT(15)
#define STM32_FMPI2C_ICR(n)        REG32(stm32_i2c_reg(n, 0x1C))

#define STM32_FMPI2C_PECR(n)       REG32(stm32_i2c_reg(n, 0x20))
#define STM32_FMPI2C_RXDR(n)       REG32(stm32_i2c_reg(n, 0x24))
#define STM32_FMPI2C_TXDR(n)       REG32(stm32_i2c_reg(n, 0x28))
#endif

/* --- Power / Reset / Clocks --- */
#define STM32_PWR_CR                REG32(STM32_PWR_BASE + 0x00)
#define STM32_PWR_CR_LPSDSR		(1 << 0)
#define STM32_PWR_CR_FLPS		(1 << 9)
#define STM32_PWR_CR_SVOS5		(1 << 14)
#define STM32_PWR_CR_SVOS4		(2 << 14)
#define STM32_PWR_CR_SVOS3		(3 << 14)
#define STM32_PWR_CR_SVOS_MASK		(3 << 14)

#if defined(CHIP_FAMILY_STM32L4)
#define STM32_PWR_CR2               REG32(STM32_PWR_BASE + 0x04)
#define STM32_PWR_CSR               REG32(STM32_PWR_BASE + 0x10)
#else
#define STM32_PWR_CSR               REG32(STM32_PWR_BASE + 0x04)
#ifdef CHIP_FAMILY_STM32H7
#define STM32_PWR_CR2               REG32(STM32_PWR_BASE + 0x08)
#define STM32_PWR_CR3               REG32(STM32_PWR_BASE + 0x0C)
#define STM32_PWR_CR3_BYPASS        BIT(0)
#define STM32_PWR_CR3_LDOEN         BIT(1)
#define STM32_PWR_CR3_SCUEN         BIT(2)
#define STM32_PWR_CR3_VBE           BIT(8)
#define STM32_PWR_CR3_VBRS          BIT(9)
#define STM32_PWR_CR3_USB33DEN      BIT(24)
#define STM32_PWR_CR3_USBREGEN      BIT(25)
#define STM32_PWR_CR3_USB33RDY      BIT(26)
#define STM32_PWR_CPUCR             REG32(STM32_PWR_BASE + 0x10)
#define STM32_PWR_CPUCR_PDDS_D1     BIT(0)
#define STM32_PWR_CPUCR_PDDS_D2     BIT(1)
#define STM32_PWR_CPUCR_PDDS_D3     BIT(2)
#define STM32_PWR_CPUCR_STOPF       BIT(5)
#define STM32_PWR_CPUCR_SBF         BIT(6)
#define STM32_PWR_CPUCR_SBF_D1      BIT(7)
#define STM32_PWR_CPUCR_SBF_D2      BIT(8)
#define STM32_PWR_CPUCR_CSSF        BIT(9)
#define STM32_PWR_CPUCR_RUN_D3      BIT(11)
#define STM32_PWR_D3CR              REG32(STM32_PWR_BASE + 0x18)
#define STM32_PWR_D3CR_VOS1         (3 << 14)
#define STM32_PWR_D3CR_VOS2         (2 << 14)
#define STM32_PWR_D3CR_VOS3         (1 << 14)
#define STM32_PWR_D3CR_VOSMASK      (3 << 14)
#define STM32_PWR_D3CR_VOSRDY       (1 << 13)
#define STM32_PWR_WKUPCR            REG32(STM32_PWR_BASE + 0x20)
#define STM32_PWR_WKUPFR            REG32(STM32_PWR_BASE + 0x24)
#define STM32_PWR_WKUPEPR           REG32(STM32_PWR_BASE + 0x28)
#endif /* CHIP_FAMILY_STM32H7 */
#endif
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
#define STM32_PWR_CSR_EWUP1         BIT(8)
#define STM32_PWR_CSR_EWUP2         BIT(9)
#define STM32_PWR_CSR_EWUP3         BIT(10)
#define STM32_PWR_CSR_EWUP4         BIT(11) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP5         BIT(12) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP6         BIT(13) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP7         BIT(14) /* STM32F0xx only */
#define STM32_PWR_CSR_EWUP8         BIT(15) /* STM32F0xx only */
#endif

#if defined(CHIP_FAMILY_STM32L)
#define STM32_RCC_CR                REG32(STM32_RCC_BASE + 0x00)
#define STM32_RCC_CR_HSION		BIT(0)
#define STM32_RCC_CR_HSIRDY		BIT(1)
#define STM32_RCC_CR_MSION		BIT(8)
#define STM32_RCC_CR_MSIRDY		BIT(9)
#define STM32_RCC_CR_PLLON		BIT(24)
#define STM32_RCC_CR_PLLRDY		BIT(25)
#define STM32_RCC_ICSCR             REG32(STM32_RCC_BASE + 0x04)
#define STM32_RCC_ICSCR_MSIRANGE(n)	((n) << 13)
#define STM32_RCC_ICSCR_MSIRANGE_1MHZ	STM32_RCC_ICSCR_MSIRANGE(4)
#define STM32_RCC_ICSCR_MSIRANGE_2MHZ	STM32_RCC_ICSCR_MSIRANGE(5)
#define STM32_RCC_ICSCR_MSIRANGE_MASK	STM32_RCC_ICSCR_MSIRANGE(7)
#define STM32_RCC_CFGR              REG32(STM32_RCC_BASE + 0x08)
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
#define STM32_RCC_CIR               REG32(STM32_RCC_BASE + 0x0C)
#define STM32_RCC_AHBRSTR           REG32(STM32_RCC_BASE + 0x10)
#define STM32_RCC_APB2RSTR          REG32(STM32_RCC_BASE + 0x14)
#define STM32_RCC_APB1RSTR          REG32(STM32_RCC_BASE + 0x18)
#define STM32_RCC_AHBENR            REG32(STM32_RCC_BASE + 0x1C)
#define STM32_RCC_APB2ENR           REG32(STM32_RCC_BASE + 0x20)
#define STM32_RCC_SYSCFGEN		BIT(0)

#define STM32_RCC_APB1ENR           REG32(STM32_RCC_BASE + 0x24)
#define STM32_RCC_PWREN                 BIT(28)

#define STM32_RCC_AHBLPENR          REG32(STM32_RCC_BASE + 0x28)
#define STM32_RCC_APB2LPENR         REG32(STM32_RCC_BASE + 0x2C)
#define STM32_RCC_APB1LPENR         REG32(STM32_RCC_BASE + 0x30)
#define STM32_RCC_CSR               REG32(STM32_RCC_BASE + 0x34)

#define STM32_RCC_HB_DMA1		BIT(24)
#define STM32_RCC_PB2_TIM9		BIT(2)
#define STM32_RCC_PB2_TIM10		BIT(3)
#define STM32_RCC_PB2_TIM11		BIT(4)
#define STM32_RCC_PB1_USB		BIT(23)

#define STM32_SYSCFG_MEMRMP         REG32(STM32_SYSCFG_BASE + 0x00)
#define STM32_SYSCFG_PMC            REG32(STM32_SYSCFG_BASE + 0x04)
#define STM32_SYSCFG_EXTICR(n)      REG32(STM32_SYSCFG_BASE + 8 + 4 * (n))

#elif defined(CHIP_FAMILY_STM32L4)
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

#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
#define STM32_CRS_CR                REG32(STM32_CRS_BASE + 0x00) /* STM32F0XX */
#define STM32_CRS_CR_SYNCOKIE           BIT(0)
#define STM32_CRS_CR_SYNCWARNIE         BIT(1)
#define STM32_CRS_CR_ERRIE              BIT(2)
#define STM32_CRS_CR_ESYNCIE            BIT(3)
#define STM32_CRS_CR_CEN                BIT(5)
#define STM32_CRS_CR_AUTOTRIMEN         BIT(6)
#define STM32_CRS_CR_SWSYNC             BIT(7)
#define STM32_CRS_CR_TRIM(n)		(((n) & 0x3f) << 8)

#define STM32_CRS_CFGR              REG32(STM32_CRS_BASE + 0x04) /* STM32F0XX */
#define STM32_CRS_CFGR_RELOAD(n)	(((n) & 0xffff) << 0)
#define STM32_CRS_CFGR_FELIM(n)		(((n) & 0xff) << 16)
#define STM32_CRS_CFGR_SYNCDIV(n)	(((n) & 7) << 24)
#define STM32_CRS_CFGR_SYNCSRC(n)	(((n) & 3) << 28)
#define STM32_CRS_CFGR_SYNCPOL		BIT(31)

#define STM32_CRS_ISR               REG32(STM32_CRS_BASE + 0x08) /* STM32F0XX */
#define STM32_CRS_ISR_SYNCOKF		BIT(0)
#define STM32_CRS_ISR_SYNCWARNF		BIT(1)
#define STM32_CRS_ISR_ERRF		BIT(2)
#define STM32_CRS_ISR_ESYNCF		BIT(3)
#define STM32_CRS_ISR_SYNCERR		BIT(8)
#define STM32_CRS_ISR_SYNCMISS		BIT(9)
#define STM32_CRS_ISR_TRIMOVF		BIT(10)
#define STM32_CRS_ISR_FEDIR		BIT(15)
#define STM32_CRS_ISR_FECAP		(0xffff << 16)

#define STM32_CRS_ICR               REG32(STM32_CRS_BASE + 0x0c) /* STM32F0XX */
#define STM32_CRS_ICR_SYNCOKC		BIT(0)
#define STM32_CRS_ICR_SYNCWARINC	BIT(1)
#define STM32_CRS_ICR_ERRC		BIT(2)
#define STM32_CRS_ICR_ESYNCC		BIT(3)

#define STM32_RCC_CR                REG32(STM32_RCC_BASE + 0x00)
#define STM32_RCC_CFGR              REG32(STM32_RCC_BASE + 0x04)
#define STM32_RCC_CIR               REG32(STM32_RCC_BASE + 0x08)
#define STM32_RCC_APB2RSTR          REG32(STM32_RCC_BASE + 0x0c)
#define STM32_RCC_APB1RSTR          REG32(STM32_RCC_BASE + 0x10)
#define STM32_RCC_AHBENR            REG32(STM32_RCC_BASE + 0x14)
#define STM32_RCC_APB2ENR           REG32(STM32_RCC_BASE + 0x18)
#define STM32_RCC_APB2ENR_ADCEN     BIT(9) /* STM32F3/F0 */
#define STM32_RCC_APB2ENR_TIM16EN   BIT(17)
#define STM32_RCC_APB2ENR_TIM17EN   BIT(18)
#define STM32_RCC_DBGMCUEN          BIT(22)
#define STM32_RCC_SYSCFGEN          BIT(0)

#define STM32_RCC_APB1ENR           REG32(STM32_RCC_BASE + 0x1c)
#define STM32_RCC_PWREN                 BIT(28)

#define STM32_RCC_BDCR              REG32(STM32_RCC_BASE + 0x20)
#define STM32_RCC_CSR               REG32(STM32_RCC_BASE + 0x24)
/* STM32F373 */
#define STM32_RCC_CFGR2             REG32(STM32_RCC_BASE + 0x2c)
/* STM32F0XX and STM32F373 */
#define STM32_RCC_CFGR3             REG32(STM32_RCC_BASE + 0x30)
#define STM32_RCC_CR2               REG32(STM32_RCC_BASE + 0x34) /* STM32F0XX */

#define STM32_RCC_HB_DMA1		BIT(0)
/* STM32F373 */
#define STM32_RCC_HB_DMA2		BIT(1)
#define STM32_RCC_PB2_TIM1		BIT(11) /* Except STM32F373 */
#define STM32_RCC_PB2_TIM15		BIT(16) /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB2_TIM16		BIT(17) /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB2_TIM17		BIT(18) /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB2_TIM19		BIT(19) /* STM32F373 */
#define STM32_RCC_PB2_PMAD		BIT(11) /* STM32TS */
#define STM32_RCC_PB2_PMSE		BIT(13) /* STM32TS */
#define STM32_RCC_PB1_TIM12		BIT(6)  /* STM32F373 */
#define STM32_RCC_PB1_TIM13		BIT(7)  /* STM32F373 */
#define STM32_RCC_PB1_TIM14		BIT(8)  /* STM32F0XX and STM32F373 */
#define STM32_RCC_PB1_TIM18		BIT(9)  /* STM32F373 */
#define STM32_RCC_PB1_USB		BIT(23)
#define STM32_RCC_PB1_CRS		BIT(27)

#define STM32_SYSCFG_CFGR1          REG32(STM32_SYSCFG_BASE + 0x00)
#define STM32_SYSCFG_EXTICR(n)      REG32(STM32_SYSCFG_BASE + 8 + 4 * (n))
#define STM32_SYSCFG_CFGR2          REG32(STM32_SYSCFG_BASE + 0x18)

#elif defined(CHIP_FAMILY_STM32F4)
#define STM32_RCC_CR                    REG32(STM32_RCC_BASE + 0x00)
#define STM32_RCC_CR_HSION		BIT(0)
#define STM32_RCC_CR_HSIRDY		BIT(1)
#define STM32_RCC_CR_HSEON		BIT(16)
#define STM32_RCC_CR_HSERDY		BIT(17)
#define STM32_RCC_CR_PLLON		BIT(24)
#define STM32_RCC_CR_PLLRDY		BIT(25)

#if defined(CHIP_VARIANT_STM32F446)
/* Required or recommended clocks for stm32f446 */
#define STM32F4_PLL_REQ 2000000
#define STM32F4_RTC_REQ 1000000
#define STM32F4_IO_CLOCK  42000000
#define STM32F4_USB_REQ 48000000
#define STM32F4_VCO_CLOCK 336000000
#define STM32F4_HSI_CLOCK 16000000
#define STM32F4_LSI_CLOCK 32000
#define STM32F4_TIMER_CLOCK STM32F4_IO_CLOCK
#define STM32F4_PLLP_DIV 4
#define STM32F4_AHB_PRE 0x8
#define STM32F4_APB1_PRE 0x0
#define STM32F4_APB2_PRE 0x0
#define STM32_FLASH_ACR_LATENCY     BIT(0)

#elif defined(CHIP_VARIANT_STM32F412)
/* Required or recommended clocks for stm32f412 */
#define STM32F4_PLL_REQ 2000000
#define STM32F4_RTC_REQ 1000000
#define STM32F4_IO_CLOCK  48000000
#define STM32F4_USB_REQ 48000000
#define STM32F4_VCO_CLOCK 384000000
#define STM32F4_HSI_CLOCK 16000000
#define STM32F4_LSI_CLOCK 32000
#define STM32F4_TIMER_CLOCK (STM32F4_IO_CLOCK * 2)
#define STM32F4_PLLP_DIV 4
#define STM32F4_AHB_PRE 0x0
#define STM32F4_APB1_PRE 0x4
#define STM32F4_APB2_PRE 0x4
#define STM32_FLASH_ACR_LATENCY     (3 << 0)

#elif defined(CHIP_VARIANT_STM32F411)
/* Required or recommended clocks for stm32f411 */
#define STM32F4_PLL_REQ 2000000
#define STM32F4_RTC_REQ 1000000
#define STM32F4_IO_CLOCK  48000000
#define STM32F4_USB_REQ 48000000
#define STM32F4_VCO_CLOCK 384000000
#define STM32F4_HSI_CLOCK 16000000
#define STM32F4_LSI_CLOCK 32000
#define STM32F4_TIMER_CLOCK STM32F4_IO_CLOCK
#define STM32F4_PLLP_DIV 4
#define STM32F4_AHB_PRE 0x8
#define STM32F4_APB1_PRE 0x0
#define STM32F4_APB2_PRE 0x0
#define STM32_FLASH_ACR_LATENCY     BIT(0)

#elif defined(CHIP_VARIANT_STM32F76X)
/* Required or recommended clocks for stm32f767/769 */
#define STM32F4_PLL_REQ 2000000
#define STM32F4_RTC_REQ 1000000
#define STM32F4_IO_CLOCK 45000000
#define STM32F4_USB_REQ 45000000 /* not compatible with USB, will use PLLSAI */
#define STM32F4_VCO_CLOCK 360000000
#define STM32F4_HSI_CLOCK 16000000
#define STM32F4_LSI_CLOCK 32000
#define STM32F4_TIMER_CLOCK (STM32F4_IO_CLOCK * 2)
#define STM32F4_PLLP_DIV 2   /* sys = VCO/2  = 180 Mhz */
#define STM32F4_AHB_PRE 0x0  /* AHB = sysclk = 180 Mhz */
#define STM32F4_APB1_PRE 0x5 /* APB1 = AHB /4 = 45 Mhz */
#define STM32F4_APB2_PRE 0x5 /* APB2 = AHB /4 = 45 Mhz */
#define STM32_FLASH_ACR_LATENCY     (5 << 0)

#else
#error "No valid clocks defined"
#endif

#define STM32_RCC_PLLCFGR               REG32(STM32_RCC_BASE + 0x04)
/* PLL Division factor */
#define  PLLCFGR_PLLM_OFF		0
#define  PLLCFGR_PLLM(val)		(((val) & 0x1f) << PLLCFGR_PLLM_OFF)
/* PLL Multiplication factor */
#define  PLLCFGR_PLLN_OFF		6
#define  PLLCFGR_PLLN(val)		(((val) & 0x1ff) << PLLCFGR_PLLN_OFF)
/* Main CPU Clock */
#define  PLLCFGR_PLLP_OFF		16
#define  PLLCFGR_PLLP(val)		(((val) & 0x3) << PLLCFGR_PLLP_OFF)

#define  PLLCFGR_PLLSRC_HSI		(0 << 22)
#define  PLLCFGR_PLLSRC_HSE		BIT(22)
/* USB OTG FS: Must equal 48MHz */
#define  PLLCFGR_PLLQ_OFF		24
#define  PLLCFGR_PLLQ(val)		(((val) & 0xf) << PLLCFGR_PLLQ_OFF)
/* SYSTEM */
#define  PLLCFGR_PLLR_OFF		28
#define  PLLCFGR_PLLR(val)		(((val) & 0x7) << PLLCFGR_PLLR_OFF)

#define STM32_RCC_CFGR                  REG32(STM32_RCC_BASE + 0x08)
#define STM32_RCC_CFGR_SW_HSI		(0 << 0)
#define STM32_RCC_CFGR_SW_HSE		(1 << 0)
#define STM32_RCC_CFGR_SW_PLL		(2 << 0)
#define STM32_RCC_CFGR_SW_PLL_R		(3 << 0)
#define STM32_RCC_CFGR_SW_MASK		(3 << 0)
#define STM32_RCC_CFGR_SWS_HSI		(0 << 2)
#define STM32_RCC_CFGR_SWS_HSE		(1 << 2)
#define STM32_RCC_CFGR_SWS_PLL		(2 << 2)
#define STM32_RCC_CFGR_SWS_PLL_R	(3 << 2)
#define STM32_RCC_CFGR_SWS_MASK		(3 << 2)
/* AHB Prescalar: nonlinear values, look up in RM0390 */
#define  CFGR_HPRE_OFF			4
#define  CFGR_HPRE(val)			(((val) & 0xf) << CFGR_HPRE_OFF)
/* APB1 Low Speed Prescalar < 45MHz */
#define  CFGR_PPRE1_OFF			10
#define  CFGR_PPRE1(val)		(((val) & 0x7) << CFGR_PPRE1_OFF)
/* APB2 High Speed Prescalar < 90MHz */
#define  CFGR_PPRE2_OFF			13
#define  CFGR_PPRE2(val)		(((val) & 0x7) << CFGR_PPRE2_OFF)
/* RTC CLock: Must equal 1MHz */
#define  CFGR_RTCPRE_OFF		16
#define  CFGR_RTCPRE(val)		(((val) & 0x1f) << CFGR_RTCPRE_OFF)

#define STM32_RCC_CIR                   REG32(STM32_RCC_BASE + 0x0C)
#define STM32_RCC_AHB1RSTR              REG32(STM32_RCC_BASE + 0x10)
#define  RCC_AHB1RSTR_OTGHSRST		BIT(29)

#define STM32_RCC_AHB2RSTR              REG32(STM32_RCC_BASE + 0x14)
#define STM32_RCC_AHB3RSTR              REG32(STM32_RCC_BASE + 0x18)

#define STM32_RCC_APB1RSTR              REG32(STM32_RCC_BASE + 0x20)
#define STM32_RCC_APB2RSTR              REG32(STM32_RCC_BASE + 0x24)

#define STM32_RCC_AHB1ENR               REG32(STM32_RCC_BASE + 0x30)
#define STM32_RCC_AHB1ENR_GPIOMASK	(0xff << 0)
#define STM32_RCC_AHB1ENR_BKPSRAMEN	BIT(18)
#define STM32_RCC_AHB1ENR_DMA1EN	BIT(21)
#define STM32_RCC_AHB1ENR_DMA2EN	BIT(22)
/* TODO(nsanders): normalize naming.*/
#define STM32_RCC_HB1_DMA1		BIT(21)
#define STM32_RCC_HB1_DMA2		BIT(22)
#define STM32_RCC_AHB1ENR_OTGHSEN	BIT(29)
#define STM32_RCC_AHB1ENR_OTGHSULPIEN	BIT(30)

#define STM32_RCC_AHB2ENR               REG32(STM32_RCC_BASE + 0x34)
#define STM32_RCC_AHB2ENR_RNGEN		BIT(6)
#define STM32_RCC_AHB2ENR_OTGFSEN	BIT(7)
#define STM32_RCC_AHB3ENR               REG32(STM32_RCC_BASE + 0x38)

#define STM32_RCC_APB1ENR               REG32(STM32_RCC_BASE + 0x40)
#define STM32_RCC_PWREN                 BIT(28)
#define STM32_RCC_I2C1EN                BIT(21)
#define STM32_RCC_I2C2EN                BIT(22)
#define STM32_RCC_I2C3EN                BIT(23)
#define STM32_RCC_FMPI2C4EN             BIT(24)

#define STM32_RCC_APB2ENR               REG32(STM32_RCC_BASE + 0x44)
#define STM32_RCC_APB2ENR_ADC1EN        BIT(8) /* STM32F4 */

#define STM32_RCC_PB2_USART6            BIT(5)
#define STM32_RCC_SYSCFGEN		BIT(14)

#define STM32_RCC_AHB1LPENR             REG32(STM32_RCC_BASE + 0x50)
#define STM32_RCC_AHB2LPENR             REG32(STM32_RCC_BASE + 0x54)
#define STM32_RCC_AHB3LPENR             REG32(STM32_RCC_BASE + 0x58)
#define STM32_RCC_APB1LPENR             REG32(STM32_RCC_BASE + 0x60)
#define STM32_RCC_APB2LPENR             REG32(STM32_RCC_BASE + 0x64)

#define STM32_RCC_BDCR                  REG32(STM32_RCC_BASE + 0x70)
#define STM32_RCC_CSR                   REG32(STM32_RCC_BASE + 0x74)
#define STM32_RCC_CSR_LSION		BIT(0)
#define STM32_RCC_CSR_LSIRDY		BIT(1)

#if defined(CHIP_FAMILY_STM32F4)
#define STM32_RCC_PB2_TIM9		BIT(16)
#define STM32_RCC_PB2_TIM10		BIT(17)
#define STM32_RCC_PB2_TIM11		BIT(18)
#else /* !defined(CHIP_FAMILY_STM32F4) */
#define STM32_RCC_HB_DMA1		BIT(24)
#define STM32_RCC_PB2_TIM9		BIT(2)
#define STM32_RCC_PB2_TIM10		BIT(3)
#define STM32_RCC_PB2_TIM11		BIT(4)
#define STM32_RCC_PB1_USB		BIT(23)
#endif

#define STM32_RCC_DCKCFGR2              REG32(STM32_RCC_BASE + 0x94)
#define  DCKCFGR2_FMPI2C1SEL(val)       (((val) & 0x3) << 22)
#define  DCKCFGR2_FMPI2C1SEL_MASK       (0x3 << 22)
#define  FMPI2C1SEL_APB                 0x0

#define STM32_SYSCFG_MEMRMP             REG32(STM32_SYSCFG_BASE + 0x00)
#define STM32_SYSCFG_PMC                REG32(STM32_SYSCFG_BASE + 0x04)
#define STM32_SYSCFG_EXTICR(n)          REG32(STM32_SYSCFG_BASE + 8 + 4 * (n))
#define STM32_SYSCFG_CMPCR              REG32(STM32_SYSCFG_BASE + 0x20)
#define STM32_SYSCFG_CFGR               REG32(STM32_SYSCFG_BASE + 0x2C)

#elif defined(CHIP_FAMILY_STM32H7)
#define STM32_RCC_CR                REG32(STM32_RCC_BASE + 0x000)
#define STM32_RCC_ICSCR             REG32(STM32_RCC_BASE + 0x004)
#define STM32_RCC_CRRCR             REG32(STM32_RCC_BASE + 0x008)
#define STM32_RCC_CFGR              REG32(STM32_RCC_BASE + 0x010)
#define STM32_RCC_D1CFGR            REG32(STM32_RCC_BASE + 0x018)
#define STM32_RCC_D2CFGR            REG32(STM32_RCC_BASE + 0x01C)
#define STM32_RCC_D3CFGR            REG32(STM32_RCC_BASE + 0x020)
#define STM32_RCC_PLLCKSELR         REG32(STM32_RCC_BASE + 0x028)
#define STM32_RCC_PLLCFGR           REG32(STM32_RCC_BASE + 0x02C)
#define STM32_RCC_PLL1DIVR          REG32(STM32_RCC_BASE + 0x030)
#define STM32_RCC_PLL1FRACR         REG32(STM32_RCC_BASE + 0x034)
#define STM32_RCC_PLL2DIVR          REG32(STM32_RCC_BASE + 0x038)
#define STM32_RCC_PLL2FRACR         REG32(STM32_RCC_BASE + 0x03C)
#define STM32_RCC_PLL3DIVR          REG32(STM32_RCC_BASE + 0x040)
#define STM32_RCC_PLL3FRACR         REG32(STM32_RCC_BASE + 0x044)
#define STM32_RCC_D1CCIPR           REG32(STM32_RCC_BASE + 0x04C)
#define STM32_RCC_D2CCIP1R          REG32(STM32_RCC_BASE + 0x050)
#define STM32_RCC_D2CCIP2R          REG32(STM32_RCC_BASE + 0x054)
#define STM32_RCC_D3CCIPR           REG32(STM32_RCC_BASE + 0x058)
#define STM32_RCC_CIER              REG32(STM32_RCC_BASE + 0x060)
#define STM32_RCC_CIFR              REG32(STM32_RCC_BASE + 0x064)
#define STM32_RCC_CICR              REG32(STM32_RCC_BASE + 0x068)
#define STM32_RCC_BDCR              REG32(STM32_RCC_BASE + 0x070)
#define STM32_RCC_CSR               REG32(STM32_RCC_BASE + 0x074)

#define STM32_RCC_APB2RSTR          REG32(STM32_RCC_BASE + 0x098)

#define STM32_RCC_RSR               REG32(STM32_RCC_BASE + 0x0D0)
#define STM32_RCC_AHB3ENR           REG32(STM32_RCC_BASE + 0x0D4)
#define STM32_RCC_AHB1ENR           REG32(STM32_RCC_BASE + 0x0D8)
#define STM32_RCC_AHB2ENR           REG32(STM32_RCC_BASE + 0x0DC)
#define STM32_RCC_AHB2ENR_RNGEN     BIT(6)
#define STM32_RCC_AHB2ENR_HASHEN    BIT(5)
#define STM32_RCC_AHB2ENR_CRYPTEN   BIT(4)
#define STM32_RCC_AHB4ENR           REG32(STM32_RCC_BASE + 0x0E0)
#define STM32_RCC_AHB4ENR_GPIOMASK  0x3ff
#define STM32_RCC_APB3ENR           REG32(STM32_RCC_BASE + 0x0E4)
#define STM32_RCC_APB1LENR          REG32(STM32_RCC_BASE + 0x0E8)
#define STM32_RCC_APB1HENR          REG32(STM32_RCC_BASE + 0x0EC)
#define STM32_RCC_APB2ENR           REG32(STM32_RCC_BASE + 0x0F0)
#define STM32_RCC_APB4ENR           REG32(STM32_RCC_BASE + 0x0F4)
#define STM32_RCC_SYSCFGEN          BIT(1)
#define STM32_RCC_AHB3LPENR         REG32(STM32_RCC_BASE + 0x0FC)
#define STM32_RCC_AHB1LPENR         REG32(STM32_RCC_BASE + 0x100)
#define STM32_RCC_AHB2LPENR         REG32(STM32_RCC_BASE + 0x104)
#define STM32_RCC_AHB4LPENR         REG32(STM32_RCC_BASE + 0x108)
#define STM32_RCC_APB3LPENR         REG32(STM32_RCC_BASE + 0x10C)
#define STM32_RCC_APB1LLPENR        REG32(STM32_RCC_BASE + 0x110)
#define STM32_RCC_APB1HLPENR        REG32(STM32_RCC_BASE + 0x114)
#define STM32_RCC_APB2LPENR         REG32(STM32_RCC_BASE + 0x118)
#define STM32_RCC_APB4LPENR         REG32(STM32_RCC_BASE + 0x11C)
/* Aliases */
#define STM32_RCC_APB1ENR           STM32_RCC_APB1LENR

#define STM32_RCC_CR_HSION                     BIT(0)
#define STM32_RCC_CR_HSIRDY                    BIT(2)
#define STM32_RCC_CR_CSION                     BIT(7)
#define STM32_RCC_CR_CSIRDY                    BIT(8)
#define STM32_RCC_CR_HSI48ON                   BIT(12)
#define STM32_RCC_CR_HSI48RDY                  BIT(13)
#define STM32_RCC_CR_PLL1ON                    BIT(24)
#define STM32_RCC_CR_PLL1RDY                   BIT(25)
#define STM32_RCC_CR_PLL2ON                    BIT(26)
#define STM32_RCC_CR_PLL2RDY                   BIT(27)
#define STM32_RCC_CR_PLL3ON                    BIT(28)
#define STM32_RCC_CR_PLL3RDY                   BIT(29)
#define STM32_RCC_CFGR_SW_HSI                  (0 << 0)
#define STM32_RCC_CFGR_SW_CSI                  (1 << 0)
#define STM32_RCC_CFGR_SW_HSE                  (2 << 0)
#define STM32_RCC_CFGR_SW_PLL1                 (3 << 0)
#define STM32_RCC_CFGR_SW_MASK                 (3 << 0)
#define STM32_RCC_CFGR_SWS_HSI                 (0 << 3)
#define STM32_RCC_CFGR_SWS_CSI                 (1 << 3)
#define STM32_RCC_CFGR_SWS_HSE                 (2 << 3)
#define STM32_RCC_CFGR_SWS_PLL1                (3 << 3)
#define STM32_RCC_CFGR_SWS_MASK                (3 << 3)
#define STM32_RCC_D1CFGR_HPRE_DIV1             (0 << 0)
#define STM32_RCC_D1CFGR_HPRE_DIV2             (8 << 0)
#define STM32_RCC_D1CFGR_HPRE_DIV4             (9 << 0)
#define STM32_RCC_D1CFGR_HPRE_DIV8            (10 << 0)
#define STM32_RCC_D1CFGR_HPRE_DIV16           (11 << 0)
#define STM32_RCC_D1CFGR_D1PPRE_DIV1           (0 << 4)
#define STM32_RCC_D1CFGR_D1PPRE_DIV2           (4 << 4)
#define STM32_RCC_D1CFGR_D1PPRE_DIV4           (5 << 4)
#define STM32_RCC_D1CFGR_D1PPRE_DIV8           (6 << 4)
#define STM32_RCC_D1CFGR_D1PPRE_DIV16          (7 << 4)
#define STM32_RCC_D1CFGR_D1CPRE_DIV1           (0 << 8)
#define STM32_RCC_D1CFGR_D1CPRE_DIV2           (8 << 8)
#define STM32_RCC_D1CFGR_D1CPRE_DIV4           (9 << 8)
#define STM32_RCC_D1CFGR_D1CPRE_DIV8          (10 << 8)
#define STM32_RCC_D1CFGR_D1CPRE_DIV16         (1BIT(8))
#define STM32_RCC_PLLCKSEL_PLLSRC_HSI          (0 << 0)
#define STM32_RCC_PLLCKSEL_PLLSRC_CSI          (1 << 0)
#define STM32_RCC_PLLCKSEL_PLLSRC_HSE          (2 << 0)
#define STM32_RCC_PLLCKSEL_PLLSRC_NONE         (3 << 0)
#define STM32_RCC_PLLCKSEL_PLLSRC_MASK         (3 << 0)
#define STM32_RCC_PLLCKSEL_DIVM1(m)            ((m) << 4)
#define STM32_RCC_PLLCKSEL_DIVM2(m)            ((m) << 12)
#define STM32_RCC_PLLCKSEL_DIVM3(m)            ((m) << 20)
#define STM32_RCC_PLLCFG_PLL1VCOSEL_FRACEN     BIT(0)
#define STM32_RCC_PLLCFG_PLL1VCOSEL_WIDE       (0 << 1)
#define STM32_RCC_PLLCFG_PLL1VCOSEL_MEDIUM     BIT(1)
#define STM32_RCC_PLLCFG_PLL1RGE_1M_2M         (0 << 2)
#define STM32_RCC_PLLCFG_PLL1RGE_2M_4M         (1 << 2)
#define STM32_RCC_PLLCFG_PLL1RGE_4M_8M         (2 << 2)
#define STM32_RCC_PLLCFG_PLL1RGE_8M_16M        (3 << 2)
#define STM32_RCC_PLLCFG_DIVP1EN               BIT(16)
#define STM32_RCC_PLLCFG_DIVQ1EN               BIT(17)
#define STM32_RCC_PLLCFG_DIVR1EN               BIT(18)
#define STM32_RCC_PLLDIV_DIVN(n)               (((n) - 1) << 0)
#define STM32_RCC_PLLDIV_DIVP(p)               (((p) - 1) << 9)
#define STM32_RCC_PLLDIV_DIVQ(q)               (((q) - 1) << 16)
#define STM32_RCC_PLLDIV_DIVR(r)               (((r) - 1) << 24)
#define STM32_RCC_PLLFRAC(n)                   ((n) << 3)
#define STM32_RCC_D2CCIP1R_SPI123SEL_PLL1Q     (0 << 12)
#define STM32_RCC_D2CCIP1R_SPI123SEL_PLL2P     (1 << 12)
#define STM32_RCC_D2CCIP1R_SPI123SEL_PLL3P     (2 << 12)
#define STM32_RCC_D2CCIP1R_SPI123SEL_I2SCKIN   (3 << 12)
#define STM32_RCC_D2CCIP1R_SPI123SEL_PERCK     (4 << 12)
#define STM32_RCC_D2CCIP1R_SPI123SEL_MASK      (7 << 12)
#define STM32_RCC_D2CCIP1R_SPI45SEL_APB        (0 << 16)
#define STM32_RCC_D2CCIP1R_SPI45SEL_PLL2Q      (1 << 16)
#define STM32_RCC_D2CCIP1R_SPI45SEL_PLL3Q      (2 << 16)
#define STM32_RCC_D2CCIP1R_SPI45SEL_HSI        (3 << 16)
#define STM32_RCC_D2CCIP1R_SPI45SEL_CSI        (4 << 16)
#define STM32_RCC_D2CCIP1R_SPI45SEL_HSE        (5 << 16)
#define STM32_RCC_D2CCIP1R_SPI45SEL_MASK       (7 << 16)
#define STM32_RCC_D2CCIP2_USART234578SEL_PCLK  (0 << 0)
#define STM32_RCC_D2CCIP2_USART234578SEL_PLL2Q (1 << 0)
#define STM32_RCC_D2CCIP2_USART234578SEL_PLL3Q (2 << 0)
#define STM32_RCC_D2CCIP2_USART234578SEL_HSI   (3 << 0)
#define STM32_RCC_D2CCIP2_USART234578SEL_CSI   (4 << 0)
#define STM32_RCC_D2CCIP2_USART234578SEL_LSE   (5 << 0)
#define STM32_RCC_D2CCIP2_USART234578SEL_MASK  (7 << 0)
#define STM32_RCC_D2CCIP2_USART16SEL_PCLK      (0 << 3)
#define STM32_RCC_D2CCIP2_USART16SEL_PLL2Q     (1 << 3)
#define STM32_RCC_D2CCIP2_USART16SEL_PLL3Q     (2 << 3)
#define STM32_RCC_D2CCIP2_USART16SEL_HSI       (3 << 3)
#define STM32_RCC_D2CCIP2_USART16SEL_CSI       (4 << 3)
#define STM32_RCC_D2CCIP2_USART16SEL_LSE       (5 << 3)
#define STM32_RCC_D2CCIP2_USART16SEL_MASK      (7 << 3)
#define STM32_RCC_D2CCIP2_RNGSEL_HSI48         (0 << 8)
#define STM32_RCC_D2CCIP2_RNGSEL_PLL1Q         (1 << 8)
#define STM32_RCC_D2CCIP2_RNGSEL_LSE           (2 << 8)
#define STM32_RCC_D2CCIP2_RNGSEL_LSI           (3 << 8)
#define STM32_RCC_D2CCIP2_RNGSEL_MASK          (3 << 8)
#define STM32_RCC_D2CCIP2_LPTIM1SEL_PCLK       (0 << 28)
#define STM32_RCC_D2CCIP2_LPTIM1SEL_PLL2       (1 << 28)
#define STM32_RCC_D2CCIP2_LPTIM1SEL_PLL3       (2 << 28)
#define STM32_RCC_D2CCIP2_LPTIM1SEL_LSE        (3 << 28)
#define STM32_RCC_D2CCIP2_LPTIM1SEL_LSI        (4 << 28)
#define STM32_RCC_D2CCIP2_LPTIM1SEL_PER        (5 << 28)
#define STM32_RCC_D2CCIP2_LPTIM1SEL_MASK       (7 << 28)
#define STM32_RCC_CSR_LSION                    BIT(0)
#define STM32_RCC_CSR_LSIRDY                   BIT(1)

#define STM32_SYSCFG_PMCR           REG32(STM32_SYSCFG_BASE + 0x04)
#define STM32_SYSCFG_EXTICR(n)      REG32(STM32_SYSCFG_BASE + 8 + 4 * (n))

/* Peripheral bits for APB1ENR regs */
#define STM32_RCC_PB1_LPTIM1            BIT(9)

/* Peripheral bits for APB2ENR regs */
#define STM32_RCC_PB2_TIM1              BIT(0)
#define STM32_RCC_PB2_TIM2              BIT(1)
#define STM32_RCC_PB2_USART1            BIT(4)
#define STM32_RCC_PB2_SPI1              BIT(12)
#define STM32_RCC_PB2_SPI4              BIT(13)
#define STM32_RCC_PB2_TIM15             BIT(16)
#define STM32_RCC_PB2_TIM16             BIT(17)
#define STM32_RCC_PB2_TIM17             BIT(18)

/* Peripheral bits for AHB1/2/3/4ENR regs */
#define STM32_RCC_HB1_DMA1		BIT(0)
#define STM32_RCC_HB1_DMA2		BIT(1)
#define STM32_RCC_HB3_MDMA		BIT(0)
#define STM32_RCC_HB4_BDMA		BIT(21)

#else
#error Unsupported chip variant
#endif

/* RTC domain control register */
#define STM32_RCC_BDCR_BDRST		BIT(16)
#define STM32_RCC_BDCR_RTCEN		BIT(15)
#define STM32_RCC_BDCR_LSERDY		BIT(1)
#define STM32_RCC_BDCR_LSEON		BIT(0)
#define  BDCR_RTCSEL_MASK		((0x3) << 8)
#define  BDCR_RTCSEL(source)		(((source) << 8) & BDCR_RTCSEL_MASK)
#define  BDCR_SRC_LSE			0x1
#define  BDCR_SRC_LSI			0x2
#define  BDCR_SRC_HSE			0x3

/* Peripheral bits for RCC_APB/AHB and DBGMCU regs */
#define STM32_RCC_PB1_TIM2		BIT(0)
#define STM32_RCC_PB1_TIM3		BIT(1)
#define STM32_RCC_PB1_TIM4		BIT(2)
#define STM32_RCC_PB1_TIM5		BIT(3)
#define STM32_RCC_PB1_TIM6		BIT(4)
#define STM32_RCC_PB1_TIM7		BIT(5)
#define STM32_RCC_PB1_TIM12             BIT(6) /* STM32H7 */
#define STM32_RCC_PB1_TIM13             BIT(7) /* STM32H7 */
#define STM32_RCC_PB1_TIM14             BIT(8) /* STM32H7 */
#define STM32_RCC_PB1_RTC		BIT(10) /* DBGMCU only */
#define STM32_RCC_PB1_WWDG		BIT(11)
#define STM32_RCC_PB1_IWDG		BIT(12) /* DBGMCU only */
#define STM32_RCC_PB1_SPI2		BIT(14)
#define STM32_RCC_PB1_SPI3		BIT(15)
#define STM32_RCC_PB1_USART2		BIT(17)
#define STM32_RCC_PB1_USART3		BIT(18)
#define STM32_RCC_PB1_USART4		BIT(19)
#define STM32_RCC_PB1_USART5		BIT(20)
#define STM32_RCC_PB2_SPI1		BIT(12)
#if defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32H7)
#define STM32_RCC_PB2_USART1		BIT(4)
#else
#define STM32_RCC_PB2_USART1		BIT(14)
#endif

/* Reset causes definitions */
#ifdef CHIP_FAMILY_STM32H7
#define STM32_RCC_RESET_CAUSE STM32_RCC_RSR
#define  RESET_CAUSE_WDG                0x14000000
#define  RESET_CAUSE_SFT                0x01000000
#define  RESET_CAUSE_POR                0x00800000
#define  RESET_CAUSE_PIN                0x00400000
#define  RESET_CAUSE_OTHER              0xfffe0000
#define  RESET_CAUSE_RMVF               0x00010000
/* Power cause in PWR CPUCR register (Standby&Stop modes) */
#define STM32_PWR_RESET_CAUSE STM32_PWR_CPUCR
#define STM32_PWR_RESET_CAUSE_CLR STM32_PWR_CPUCR
#define  RESET_CAUSE_SBF                0x00000040
#define  RESET_CAUSE_SBF_CLR            0x00000200

#else  /* !CHIP_FAMILY_STM32H7 */
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
#endif /* !CHIP_FAMILY_STM32H7 */

/* --- Watchdogs --- */

#define STM32_WWDG_CR               REG32(STM32_WWDG_BASE + 0x00)
#define STM32_WWDG_CFR              REG32(STM32_WWDG_BASE + 0x04)
#define STM32_WWDG_SR               REG32(STM32_WWDG_BASE + 0x08)

#define STM32_WWDG_TB_8             (3 << 7)
#define STM32_WWDG_EWI              BIT(9)

#define STM32_IWDG_KR               REG32(STM32_IWDG_BASE + 0x00)
#define STM32_IWDG_KR_UNLOCK		0x5555
#define STM32_IWDG_KR_RELOAD		0xaaaa
#define STM32_IWDG_KR_START		0xcccc
#define STM32_IWDG_PR               REG32(STM32_IWDG_BASE + 0x04)
#define STM32_IWDG_RLR              REG32(STM32_IWDG_BASE + 0x08)
#define STM32_IWDG_RLR_MAX		0x0fff
#define STM32_IWDG_SR               REG32(STM32_IWDG_BASE + 0x0C)
#define STM32_IWDG_SR_WVU		BIT(2)
#define STM32_IWDG_SR_RVU		BIT(1)
#define STM32_IWDG_SR_PVU		BIT(0)
#define STM32_IWDG_WINR             REG32(STM32_IWDG_BASE + 0x10)

/* --- Real-Time Clock --- */

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32F0) || \
	defined(CHIP_FAMILY_STM32F3) || defined(CHIP_FAMILY_STM32L4) || \
	defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32H7)
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
#if defined(CHIP_FAMILY_STM32H7) || defined(CHIP_FAMILY_STM32L4)
#define STM32_BKP_BYTES             128
#elif defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32L)
#define STM32_BKP_BYTES             80
#elif defined(CHIP_FAMILY_STM32F3)
#define STM32_BKP_BYTES             64
#else
#define STM32_BKP_BYTES             20
#endif

#else
#error Unsupported chip variant
#endif

/* --- SPI --- */

/* The SPI controller registers */
struct stm32_spi_regs {
#ifdef CHIP_FAMILY_STM32H7
	uint32_t cr1;
	uint32_t cr2;
	uint32_t cfg1;
	uint32_t cfg2;
	uint32_t ier;
	uint32_t sr;
	uint32_t ifcr;
	uint32_t _pad0;
	uint32_t txdr;
	uint32_t _pad1[3];
	uint32_t rxdr;
	uint32_t _pad2[3];
	uint32_t crcpoly;
	uint32_t rxcrcr;
	uint32_t txcrcr;
	uint32_t udrdr;
#else  /* !CHIP_FAMILY_STM32H7 */
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
#endif /* !CHIP_FAMILY_STM32H7 */
};
/* Must be volatile, or compiler optimizes out repeated accesses */
typedef volatile struct stm32_spi_regs stm32_spi_regs_t;

#define STM32_SPI1_REGS ((stm32_spi_regs_t *)STM32_SPI1_BASE)
#define STM32_SPI2_REGS ((stm32_spi_regs_t *)STM32_SPI2_BASE)
#define STM32_SPI3_REGS ((stm32_spi_regs_t *)STM32_SPI3_BASE)
#define STM32_SPI4_REGS ((stm32_spi_regs_t *)STM32_SPI4_BASE)

#ifdef CHIP_FAMILY_STM32H7
#define STM32_SPI_CR1_SPE		BIT(0)
#define STM32_SPI_CR1_CSTART		BIT(9)
#define STM32_SPI_CR1_SSI		BIT(12)
#define STM32_SPI_CR1_DIV(div)		((div) << 28)
#define STM32_SPI_CFG1_DATASIZE(n)	(((n) - 1) << 0)
#define STM32_SPI_CFG1_FTHLV(n)		(((n) - 1) << 5)
#define STM32_SPI_CFG1_UDRCFG_CONST	(0 << 9)
#define STM32_SPI_CFG1_UDRCFG_LAST_RX	(1 << 9)
#define STM32_SPI_CFG1_UDRCFG_LAST_TX	(2 << 9)
#define STM32_SPI_CFG1_UDRDET_BEGIN_FRM	(0 << 11)
#define STM32_SPI_CFG1_UDRDET_END_FRM	(1 << 11)
#define STM32_SPI_CFG1_UDRDET_BEGIN_SS	(2 << 11)
#define STM32_SPI_CFG1_RXDMAEN		BIT(14)
#define STM32_SPI_CFG1_TXDMAEN		BIT(15)
#define STM32_SPI_CFG1_CRCSIZE(n)	(((n) - 1) << 16)
#define STM32_SPI_CFG2_MSTR		BIT(22)
#define STM32_SPI_CFG2_SSM		BIT(26)
#define STM32_SPI_CFG2_AFCNTR		BIT(31)

#define STM32_SPI_SR_RXNE		BIT(0)
#define STM32_SPI_SR_UDR		BIT(5)
#define STM32_SPI_SR_FRLVL		(3 << 13)
#define STM32_SPI_SR_TXC		BIT(12)
#else  /* !CHIP_FAMILY_STM32H7 */
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
#endif /* !CHIP_FAMILY_STM32H7 */

/* --- Debug --- */

#define STM32_DBGMCU_IDCODE         REG32(STM32_DBGMCU_BASE + 0x00)
#define STM32_DBGMCU_CR             REG32(STM32_DBGMCU_BASE + 0x04)
#ifndef CHIP_FAMILY_STM32H7
#define STM32_DBGMCU_APB1FZ         REG32(STM32_DBGMCU_BASE + 0x08)
#define STM32_DBGMCU_APB2FZ         REG32(STM32_DBGMCU_BASE + 0x0C)
#else  /* CHIP_FAMILY_STM32H7 */
#define STM32_DBGMCU_APB3FZ         REG32(STM32_DBGMCU_BASE + 0x34)
#define STM32_DBGMCU_APB1LFZ        REG32(STM32_DBGMCU_BASE + 0x3C)
#define STM32_DBGMCU_APB1HFZ        REG32(STM32_DBGMCU_BASE + 0x44)
#define STM32_DBGMCU_APB2FZ         REG32(STM32_DBGMCU_BASE + 0x4C)
#define STM32_DBGMCU_APB4FZ         REG32(STM32_DBGMCU_BASE + 0x54)
/* Alias */
#define STM32_DBGMCU_APB1FZ         STM32_DBGMCU_APB1LFZ
#endif /* CHIP_FAMILY_STM32H7 */

/* --- Flash --- */

#if defined(CHIP_FAMILY_STM32L)
#define STM32_FLASH_ACR             REG32(STM32_FLASH_REGS_BASE + 0x00)
#define STM32_FLASH_ACR_LATENCY		BIT(0)
#define STM32_FLASH_ACR_PRFTEN		BIT(1)
#define STM32_FLASH_ACR_ACC64		BIT(2)
#define STM32_FLASH_PECR            REG32(STM32_FLASH_REGS_BASE + 0x04)
#define STM32_FLASH_PECR_PE_LOCK	BIT(0)
#define STM32_FLASH_PECR_PRG_LOCK	BIT(1)
#define STM32_FLASH_PECR_OPT_LOCK	BIT(2)
#define STM32_FLASH_PECR_PROG		BIT(3)
#define STM32_FLASH_PECR_ERASE		BIT(9)
#define STM32_FLASH_PECR_FPRG		BIT(10)
#define STM32_FLASH_PECR_OBL_LAUNCH	BIT(18)
#define STM32_FLASH_PDKEYR          REG32(STM32_FLASH_REGS_BASE + 0x08)
#define STM32_FLASH_PEKEYR          REG32(STM32_FLASH_REGS_BASE + 0x0c)
#define STM32_FLASH_PEKEYR_KEY1		0x89ABCDEF
#define STM32_FLASH_PEKEYR_KEY2		0x02030405
#define STM32_FLASH_PRGKEYR         REG32(STM32_FLASH_REGS_BASE + 0x10)
#define STM32_FLASH_PRGKEYR_KEY1	0x8C9DAEBF
#define STM32_FLASH_PRGKEYR_KEY2	0x13141516
#define STM32_FLASH_OPTKEYR         REG32(STM32_FLASH_REGS_BASE + 0x14)
#define STM32_FLASH_OPTKEYR_KEY1	0xFBEAD9C8
#define STM32_FLASH_OPTKEYR_KEY2	0x24252627
#define STM32_FLASH_SR              REG32(STM32_FLASH_REGS_BASE + 0x18)
#define STM32_FLASH_OBR             REG32(STM32_FLASH_REGS_BASE + 0x1c)
#define STM32_FLASH_WRPR            REG32(STM32_FLASH_REGS_BASE + 0x20)

#define STM32_OPTB_RDP                  0x00
#define STM32_OPTB_USER                 0x04
#define STM32_OPTB_WRP1L                0x08
#define STM32_OPTB_WRP1H                0x0c
#define STM32_OPTB_WRP2L                0x10
#define STM32_OPTB_WRP2H                0x14
#define STM32_OPTB_WRP3L                0x18
#define STM32_OPTB_WRP3H                0x1c

#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
#define STM32_FLASH_ACR             REG32(STM32_FLASH_REGS_BASE + 0x00)
#define STM32_FLASH_ACR_LATENCY_SHIFT (0)
#define STM32_FLASH_ACR_LATENCY_MASK  (7 << STM32_FLASH_ACR_LATENCY_SHIFT)
#define STM32_FLASH_ACR_LATENCY     BIT(0)
#define STM32_FLASH_ACR_PRFTEN      BIT(4)
#define STM32_FLASH_KEYR            REG32(STM32_FLASH_REGS_BASE + 0x04)
#define  FLASH_KEYR_KEY1                0x45670123
#define  FLASH_KEYR_KEY2                0xCDEF89AB

#define STM32_FLASH_OPTKEYR         REG32(STM32_FLASH_REGS_BASE + 0x08)
#define  FLASH_OPTKEYR_KEY1             FLASH_KEYR_KEY1
#define  FLASH_OPTKEYR_KEY2             FLASH_KEYR_KEY2
#define STM32_FLASH_SR              REG32(STM32_FLASH_REGS_BASE + 0x0c)
#define  FLASH_SR_BUSY                  BIT(0)
#define  FLASH_SR_PGERR                 BIT(2)
#define  FLASH_SR_WRPRTERR              BIT(4)
#define  FLASH_SR_ALL_ERR \
	(FLASH_SR_PGERR | FLASH_SR_WRPRTERR)
#define  FLASH_SR_EOP                   BIT(5)
#define STM32_FLASH_CR              REG32(STM32_FLASH_REGS_BASE + 0x10)
#define  FLASH_CR_PG                    BIT(0)
#define  FLASH_CR_PER                   BIT(1)
#define  FLASH_CR_OPTPG                 BIT(4)
#define  FLASH_CR_OPTER                 BIT(5)
#define  FLASH_CR_STRT                  BIT(6)
#define  FLASH_CR_LOCK                  BIT(7)
#define  FLASH_CR_OPTWRE                BIT(9)
#define  FLASH_CR_OBL_LAUNCH            BIT(13)
#define STM32_FLASH_OPT_LOCKED      (!(STM32_FLASH_CR & FLASH_CR_OPTWRE))
#define STM32_FLASH_AR              REG32(STM32_FLASH_REGS_BASE + 0x14)
#define STM32_FLASH_OBR             REG32(STM32_FLASH_REGS_BASE + 0x1c)
#define STM32_FLASH_OBR_RDP_MASK        (3 << 1)
#define STM32_FLASH_WRPR            REG32(STM32_FLASH_REGS_BASE + 0x20)

#define STM32_OPTB_RDP_OFF              0x00
#define STM32_OPTB_USER_OFF             0x02
#define STM32_OPTB_WRP_OFF(n)           (0x08 + (n&3) * 2)
#define STM32_OPTB_WRP01                0x08
#define STM32_OPTB_WRP23                0x0c

#define STM32_OPTB_COMPL_SHIFT      8

#elif defined(CHIP_FAMILY_STM32L4)
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
#define  FLASH_WRP_START(val)       ((val) & 0xff)
#define  FLASH_WRP_END(val)         (((val) >> 16) & 0xff)
#define  FLASH_WRP_RANGE(strt, end) (((end) << 16) | (strt))
#define  FLASH_WRP_RANGE_DISABLED   FLASH_WRP_RANGE(0xFF, 0x00)
#define  FLASH_WRP_MASK             FLASH_WRP_RANGE(0xFF, 0xFF)

#define STM32_OPTB_USER_RDP         REG32(STM32_OPTB_BASE + 0x00)
#define STM32_OPTB_WRP1AR           REG32(STM32_OPTB_BASE + 0x18)
#define STM32_OPTB_WRP1BR           REG32(STM32_OPTB_BASE + 0x20)

#elif defined(CHIP_FAMILY_STM32F4)
#define STM32_FLASH_ACR             REG32(STM32_FLASH_REGS_BASE + 0x00)
#define STM32_FLASH_ACR_SHIFT           0
#define STM32_FLASH_ACR_LAT_MASK        0xf
#define STM32_FLASH_ACR_PRFTEN          BIT(8)
#define STM32_FLASH_ACR_ICEN            BIT(9)
#define STM32_FLASH_ACR_DCEN            BIT(10)
#define STM32_FLASH_ACR_ICRST           BIT(11)
#define STM32_FLASH_ACR_DCRST           BIT(12)
#define STM32_FLASH_KEYR            REG32(STM32_FLASH_REGS_BASE + 0x04)
#define  FLASH_KEYR_KEY1                0x45670123
#define  FLASH_KEYR_KEY2                0xCDEF89AB
#define STM32_FLASH_OPTKEYR         REG32(STM32_FLASH_REGS_BASE + 0x08)
#define  FLASH_OPTKEYR_KEY1             0x08192A3B
#define  FLASH_OPTKEYR_KEY2             0x4C5D6E7F
#define STM32_FLASH_SR              REG32(STM32_FLASH_REGS_BASE + 0x0c)
#define  FLASH_SR_EOP                   BIT(0)
#define  FLASH_SR_OPERR                 BIT(1)
#define  FLASH_SR_WRPERR                BIT(4)
#define  FLASH_SR_PGAERR                BIT(5)
#define  FLASH_SR_PGPERR                BIT(6)
#define  FLASH_SR_PGSERR                BIT(7)
#define  FLASH_SR_RDERR                 BIT(8)
#define  FLASH_SR_ALL_ERR \
	(FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
	 FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR)
#define  FLASH_SR_BUSY                   BIT(16)
#define STM32_FLASH_CR              REG32(STM32_FLASH_REGS_BASE + 0x10)
#define  FLASH_CR_PG                    BIT(0)
#define  FLASH_CR_PER                   BIT(1)
#define  FLASH_CR_MER                   BIT(2)
#define STM32_FLASH_CR_SNB_OFFSET       (3)
#define STM32_FLASH_CR_SNB(sec) \
	(((sec) & 0xf) << STM32_FLASH_CR_SNB_OFFSET)
#define STM32_FLASH_CR_SNB_MASK         (STM32_FLASH_CR_SNB(0xf))
#define STM32_FLASH_CR_PSIZE_OFFSET     (8)
#define STM32_FLASH_CR_PSIZE(size) \
	(((size) & 0x3) << STM32_FLASH_CR_PSIZE_OFFSET)
#define STM32_FLASH_CR_PSIZE_MASK       (STM32_FLASH_CR_PSIZE(0x3))
#define  FLASH_CR_STRT                  BIT(16)
#define  FLASH_CR_LOCK                  BIT(31)
#define STM32_FLASH_OPTCR           REG32(STM32_FLASH_REGS_BASE + 0x14)
#define  FLASH_OPTLOCK                  BIT(0)
#define  FLASH_OPTSTRT                  BIT(1)
#define STM32_FLASH_BOR_LEV_OFFSET      (2)
#define STM32_FLASH_RDP_MASK            (0xFF << 8)
#define STM32_FLASH_nWRP_OFFSET         (16)
#define STM32_FLASH_nWRP(_bank)         BIT(_bank + STM32_FLASH_nWRP_OFFSET)
#define STM32_FLASH_nWRP_ALL            (0xFF << STM32_FLASH_nWRP_OFFSET)
#define STM32_FLASH_OPT_LOCKED      (STM32_FLASH_OPTCR & FLASH_OPTLOCK)

#define STM32_OPTB_RDP_USER         REG32(STM32_OPTB_BASE + 0x00)
#define STM32_OPTB_RDP_OFF              0x00
#define STM32_OPTB_USER_OFF             0x02
#define STM32_OPTB_WRP_OFF(n)       (0x08 + (n&3) * 2)
#define STM32_OPTB_WP               REG32(STM32_OPTB_BASE + 0x08)
#define STM32_OPTB_nWRP(_bank)          BIT(_bank)
#define STM32_OPTB_nWRP_ALL             (0xFF)

#define STM32_OPTB_COMPL_SHIFT      8

#define STM32_OTP_BLOCK_NB              16
#define STM32_OTP_BLOCK_SIZE            32
#define STM32_OTP_BLOCK_DATA(_block, _offset) \
	(STM32_OTP_BASE + STM32_OTP_BLOCK_SIZE * (_block) + (_offset) * 4)
#define STM32_OTP_UNLOCK_BYTE           0x00
#define STM32_OTP_LOCK_BYTE             0xFF
#define STM32_OTP_LOCK_BASE         \
	(STM32_OTP_BASE + STM32_OTP_BLOCK_NB * STM32_OTP_BLOCK_SIZE)
#define STM32_OTP_LOCK(_block) \
	(STM32_OTP_LOCK_BASE + ((_block) / 4) * 4)
#define STM32_OPT_LOCK_MASK(_block)    ((0xFF << ((_block) % 4) * 8))

#elif defined(CHIP_FAMILY_STM32H7)
#define STM32_FLASH_REG(bank, offset)     REG32(((bank) ? 0x100 : 0) + \
					  STM32_FLASH_REGS_BASE + (offset))

#define STM32_FLASH_ACR(bank)             STM32_FLASH_REG(bank, 0x00)
#define STM32_FLASH_ACR_LATENCY_SHIFT (0)
#define STM32_FLASH_ACR_LATENCY_MASK  (7 << STM32_FLASH_ACR_LATENCY_SHIFT)
#define STM32_FLASH_ACR_WRHIGHFREQ_85MHZ  (0 << 4)
#define STM32_FLASH_ACR_WRHIGHFREQ_185MHZ (1 << 4)
#define STM32_FLASH_ACR_WRHIGHFREQ_285MHZ (2 << 4)
#define STM32_FLASH_ACR_WRHIGHFREQ_385MHZ (3 << 4)

#define STM32_FLASH_KEYR(bank)            STM32_FLASH_REG(bank, 0x04)
#define  FLASH_KEYR_KEY1                  0x45670123
#define  FLASH_KEYR_KEY2                  0xCDEF89AB
#define STM32_FLASH_OPTKEYR(bank)         STM32_FLASH_REG(bank, 0x08)
#define  FLASH_OPTKEYR_KEY1               0x08192A3B
#define  FLASH_OPTKEYR_KEY2               0x4C5D6E7F
#define STM32_FLASH_CR(bank)              STM32_FLASH_REG(bank, 0x0C)
#define  FLASH_CR_LOCK                    BIT(0)
#define  FLASH_CR_PG                      BIT(1)
#define  FLASH_CR_SER                     BIT(2)
#define  FLASH_CR_BER                     BIT(3)
#define  FLASH_CR_PSIZE_BYTE              (0 << 4)
#define  FLASH_CR_PSIZE_HWORD             (1 << 4)
#define  FLASH_CR_PSIZE_WORD              (2 << 4)
#define  FLASH_CR_PSIZE_DWORD             (3 << 4)
#define  FLASH_CR_PSIZE_MASK              (3 << 4)
#define  FLASH_CR_FW                      BIT(6)
#define  FLASH_CR_STRT                    BIT(7)
#define  FLASH_CR_SNB(sec)                (((sec) & 0x7) << 8)
#define  FLASH_CR_SNB_MASK                FLASH_CR_SNB(0x7)
#define STM32_FLASH_SR(bank)              STM32_FLASH_REG(bank, 0x10)
#define  FLASH_SR_BUSY                    BIT(0)
#define  FLASH_SR_WBNE                    BIT(1)
#define  FLASH_SR_QW                      BIT(2)
#define  FLASH_SR_CRC_BUSY                BIT(3)
#define  FLASH_SR_EOP                     BIT(16)
#define  FLASH_SR_WRPERR                  BIT(17)
#define  FLASH_SR_PGSERR                  BIT(18)
#define  FLASH_SR_STRBERR                 BIT(19)
#define  FLASH_SR_INCERR                  BIT(21)
#define  FLASH_SR_OPERR                   BIT(22)
#define  FLASH_SR_RDPERR                  BIT(23)
#define  FLASH_SR_RDSERR                  BIT(24)
#define  FLASH_SR_SNECCERR                BIT(25)
#define  FLASH_SR_DBECCERR                BIT(26)
#define  FLASH_SR_CRCEND                  BIT(27)
#define STM32_FLASH_CCR(bank)             STM32_FLASH_REG(bank, 0x14)
#define  FLASH_CCR_ERR_MASK              (FLASH_SR_WRPERR | FLASH_SR_PGSERR \
					| FLASH_SR_STRBERR | FLASH_SR_INCERR \
					| FLASH_SR_OPERR | FLASH_SR_RDPERR \
					| FLASH_SR_RDSERR | FLASH_SR_SNECCERR \
					| FLASH_SR_DBECCERR)
#define STM32_FLASH_OPTCR(bank)           STM32_FLASH_REG(bank, 0x18)
#define  FLASH_OPTCR_OPTLOCK              BIT(0)
#define  FLASH_OPTCR_OPTSTART             BIT(1)
#define STM32_FLASH_OPTSR_CUR(bank)       STM32_FLASH_REG(bank, 0x1C)
#define STM32_FLASH_OPTSR_PRG(bank)       STM32_FLASH_REG(bank, 0x20)
#define  FLASH_OPTSR_BUSY                 BIT(0)   /* only in OPTSR_CUR */
#define  FLASH_OPTSR_RDP_MASK             (0xFF << 8)
#define  FLASH_OPTSR_RDP_LEVEL_0          (0xAA << 8)
/* RDP Level 1: Anything but 0xAA/0xCC */
#define  FLASH_OPTSR_RDP_LEVEL_1          (0x00 << 8)
#define  FLASH_OPTSR_RDP_LEVEL_2          (0xCC << 8)
#define  FLASH_OPTSR_RSS1                 BIT(26)
#define  FLASH_OPTSR_RSS2                 BIT(27)
#define STM32_FLASH_OPTCCR(bank)          STM32_FLASH_REG(bank, 0x24)
#define STM32_FLASH_PRAR_CUR(bank)        STM32_FLASH_REG(bank, 0x28)
#define STM32_FLASH_PRAR_PRG(bank)        STM32_FLASH_REG(bank, 0x2C)
#define STM32_FLASH_SCAR_CUR(bank)        STM32_FLASH_REG(bank, 0x30)
#define STM32_FLASH_SCAR_PRG(bank)        STM32_FLASH_REG(bank, 0x34)
#define STM32_FLASH_WPSN_CUR(bank)        STM32_FLASH_REG(bank, 0x38)
#define STM32_FLASH_WPSN_PRG(bank)        STM32_FLASH_REG(bank, 0x3C)
#define STM32_FLASH_BOOT_CUR(bank)        STM32_FLASH_REG(bank, 0x40)
#define STM32_FLASH_BOOT_PRG(bank)        STM32_FLASH_REG(bank, 0x44)
#define STM32_FLASH_CRC_CR(bank)          STM32_FLASH_REG(bank, 0x50)
#define STM32_FLASH_CRC_SADDR(bank)       STM32_FLASH_REG(bank, 0x54)
#define STM32_FLASH_CRC_EADDR(bank)       STM32_FLASH_REG(bank, 0x58)
#define STM32_FLASH_CRC_DATA(bank)        STM32_FLASH_REG(bank, 0x5C)
#define STM32_FLASH_ECC_FA(bank)          STM32_FLASH_REG(bank, 0x60)

#else
#error Unsupported chip variant
#endif

/* --- External Interrupts --- */
#ifndef CHIP_FAMILY_STM32H7
#define STM32_EXTI_IMR              REG32(STM32_EXTI_BASE + 0x00)
#define STM32_EXTI_EMR              REG32(STM32_EXTI_BASE + 0x04)
#define STM32_EXTI_RTSR             REG32(STM32_EXTI_BASE + 0x08)
#define STM32_EXTI_FTSR             REG32(STM32_EXTI_BASE + 0x0c)
#define STM32_EXTI_SWIER            REG32(STM32_EXTI_BASE + 0x10)
#define STM32_EXTI_PR               REG32(STM32_EXTI_BASE + 0x14)
#else  /* CHIP_FAMILY_STM32H7 */
#define STM32_EXTI_RTSR1            REG32(STM32_EXTI_BASE + 0x00)
#define STM32_EXTI_FTSR1            REG32(STM32_EXTI_BASE + 0x04)
#define STM32_EXTI_SWIER1           REG32(STM32_EXTI_BASE + 0x08)
#define STM32_EXTI_D3PMR1           REG32(STM32_EXTI_BASE + 0x0C)
#define STM32_EXTI_D3PCR1L          REG32(STM32_EXTI_BASE + 0x10)
#define STM32_EXTI_D3PCR1H          REG32(STM32_EXTI_BASE + 0x14)
#define STM32_EXTI_RTSR2            REG32(STM32_EXTI_BASE + 0x20)
#define STM32_EXTI_FTSR2            REG32(STM32_EXTI_BASE + 0x24)
#define STM32_EXTI_SWIER2           REG32(STM32_EXTI_BASE + 0x28)
#define STM32_EXTI_D3PMR2           REG32(STM32_EXTI_BASE + 0x2C)
#define STM32_EXTI_D3PCR2L          REG32(STM32_EXTI_BASE + 0x30)
#define STM32_EXTI_D3PCR2H          REG32(STM32_EXTI_BASE + 0x34)
#define STM32_EXTI_RTSR3            REG32(STM32_EXTI_BASE + 0x40)
#define STM32_EXTI_FTSR3            REG32(STM32_EXTI_BASE + 0x44)
#define STM32_EXTI_SWIER3           REG32(STM32_EXTI_BASE + 0x48)
#define STM32_EXTI_D3PMR3           REG32(STM32_EXTI_BASE + 0x4C)
#define STM32_EXTI_D3PCR3L          REG32(STM32_EXTI_BASE + 0x50)
#define STM32_EXTI_D3PCR3H          REG32(STM32_EXTI_BASE + 0x54)
#define STM32_EXTI_CPUIMR1          REG32(STM32_EXTI_BASE + 0x80)
#define STM32_EXTI_CPUIER1          REG32(STM32_EXTI_BASE + 0x84)
#define STM32_EXTI_CPUPR1           REG32(STM32_EXTI_BASE + 0x88)
#define STM32_EXTI_CPUIMR2          REG32(STM32_EXTI_BASE + 0x90)
#define STM32_EXTI_CPUIER2          REG32(STM32_EXTI_BASE + 0x94)
#define STM32_EXTI_CPUPR2           REG32(STM32_EXTI_BASE + 0x98)
#define STM32_EXTI_CPUIMR3          REG32(STM32_EXTI_BASE + 0xA0)
#define STM32_EXTI_CPUIER3          REG32(STM32_EXTI_BASE + 0xA4)
#define STM32_EXTI_CPUPR3           REG32(STM32_EXTI_BASE + 0xA8)
/* Aliases */
#define STM32_EXTI_IMR              STM32_EXTI_CPUIMR1
#define STM32_EXTI_EMR              STM32_EXTI_CPUIMR1
#define STM32_EXTI_RTSR             STM32_EXTI_RTSR1
#define STM32_EXTI_FTSR             STM32_EXTI_FTSR1
#define STM32_EXTI_SWIER            STM32_EXTI_SWIER1
#define STM32_EXTI_PR               STM32_EXTI_CPUPR1
#endif /* CHIP_FAMILY_STM32H7 */

#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3) || \
	defined(CHIP_FAMILY_STM32F4)
#define EXTI_RTC_ALR_EVENT BIT(17)
#endif

/* --- ADC --- */
#if defined(CHIP_VARIANT_STM32F373) || defined(CHIP_FAMILY_STM32F4)
#define STM32_ADC_SR               REG32(STM32_ADC1_BASE + 0x00)
#define STM32_ADC_CR1              REG32(STM32_ADC1_BASE + 0x04)
#define STM32_ADC_CR2              REG32(STM32_ADC1_BASE + 0x08)
#define STM32_ADC_CR2_ADON         BIT(0)
#define STM32_ADC_CR2_CONT         BIT(1)
#define STM32_ADC_CR2_CAL          BIT(2)
#define STM32_ADC_CR2_RSTCAL       BIT(3)
#define STM32_ADC_CR2_ALIGN        BIT(11)
#define STM32_ADC_CR2_SWSTART      BIT(30)
#define STM32_ADC_SMPR1            REG32(STM32_ADC1_BASE + 0x0C)
#define STM32_ADC_SMPR2            REG32(STM32_ADC1_BASE + 0x10)
#define STM32_ADC_JOFR(n)          REG32(STM32_ADC1_BASE + 0x14 + ((n)&3) * 4)
#define STM32_ADC_HTR              REG32(STM32_ADC1_BASE + 0x24)
#define STM32_ADC_LTR              REG32(STM32_ADC1_BASE + 0x28)
#define STM32_ADC_SQR(n)           REG32(STM32_ADC1_BASE + 0x28 + ((n)&3) * 4)
#define STM32_ADC_SQR1             REG32(STM32_ADC1_BASE + 0x2C)
#define STM32_ADC_SQR2             REG32(STM32_ADC1_BASE + 0x30)
#define STM32_ADC_SQR3             REG32(STM32_ADC1_BASE + 0x34)
#define STM32_ADC_JSQR             REG32(STM32_ADC1_BASE + 0x38)
#define STM32_ADC_JDR(n)           REG32(STM32_ADC1_BASE + 0x3C + ((n)&3) * 4)
#define STM32_ADC_DR               REG32(STM32_ADC1_BASE + 0x4C)
#elif defined(CHIP_FAMILY_STM32F0)
#define STM32_ADC_ISR              REG32(STM32_ADC1_BASE + 0x00)
#define STM32_ADC_ISR_ADRDY        BIT(0)
#define STM32_ADC_IER              REG32(STM32_ADC1_BASE + 0x04)
#define STM32_ADC_IER_AWDIE        BIT(7)
#define STM32_ADC_IER_OVRIE        BIT(4)
#define STM32_ADC_IER_EOSEQIE      BIT(3)
#define STM32_ADC_IER_EOCIE        BIT(2)
#define STM32_ADC_IER_EOSMPIE      BIT(1)
#define STM32_ADC_IER_ADRDYIE      BIT(0)

#define STM32_ADC_CR               REG32(STM32_ADC1_BASE + 0x08)
#define STM32_ADC_CR_ADEN          BIT(0)
#define STM32_ADC_CR_ADDIS         BIT(1)
#define STM32_ADC_CR_ADCAL         BIT(31)
#define STM32_ADC_CFGR1            REG32(STM32_ADC1_BASE + 0x0C)
/* Analog watchdog channel selection */
#define STM32_ADC_CFGR1_AWDCH_MASK (0x1f << 26)
#define STM32_ADC_CFGR1_AWDEN      BIT(23)
#define STM32_ADC_CFGR1_AWDSGL     BIT(22)
/* Selects single vs continuous */
#define STM32_ADC_CFGR1_CONT       BIT(13)
/* Selects ADC_DR overwrite vs preserve */
#define STM32_ADC_CFGR1_OVRMOD     BIT(12)
/* External trigger polarity selection */
#define STM32_ADC_CFGR1_EXTEN_DIS  (0 << 10)
#define STM32_ADC_CFGR1_EXTEN_RISE (1 << 10)
#define STM32_ADC_CFGR1_EXTEN_FALL (2 << 10)
#define STM32_ADC_CFGR1_EXTEN_BOTH (3 << 10)
#define STM32_ADC_CFGR1_EXTEN_MASK (3 << 10)
/* External trigger selection */
#define STM32_ADC_CFGR1_TRG0	   (0 << 6)
#define STM32_ADC_CFGR1_TRG1	   (1 << 6)
#define STM32_ADC_CFGR1_TRG2	   (2 << 6)
#define STM32_ADC_CFGR1_TRG3	   (3 << 6)
#define STM32_ADC_CFGR1_TRG4	   (4 << 6)
#define STM32_ADC_CFGR1_TRG5	   (5 << 6)
#define STM32_ADC_CFGR1_TRG6	   (6 << 6)
#define STM32_ADC_CFGR1_TRG7	   (7 << 6)
#define STM32_ADC_CFGR1_TRG_MASK   (7 << 6)
/* Selects circular vs one-shot */
#define STM32_ADC_CFGR1_DMACFG     BIT(1)
#define STM32_ADC_CFGR1_DMAEN      BIT(0)
#define STM32_ADC_CFGR2            REG32(STM32_ADC1_BASE + 0x10)
/* Sampling time selection - 1.5 ADC cycles min, 239.5 cycles max */
#define STM32_ADC_SMPR             REG32(STM32_ADC1_BASE + 0x14)
#define STM32_ADC_SMPR_1_5_CY      0x0
#define STM32_ADC_SMPR_7_5_CY      0x1
#define STM32_ADC_SMPR_13_5_CY     0x2
#define STM32_ADC_SMPR_28_5_CY     0x3
#define STM32_ADC_SMPR_41_5_CY     0x4
#define STM32_ADC_SMPR_55_5_CY     0x5
#define STM32_ADC_SMPR_71_5_CY     0x6
#define STM32_ADC_SMPR_239_5_CY    0x7
#define STM32_ADC_TR               REG32(STM32_ADC1_BASE + 0x20)
#define STM32_ADC_CHSELR           REG32(STM32_ADC1_BASE + 0x28)
#define STM32_ADC_DR               REG32(STM32_ADC1_BASE + 0x40)
#define STM32_ADC_CCR              REG32(STM32_ADC1_BASE + 0x308)
#elif defined(CHIP_FAMILY_STM32L)
#define STM32_ADC_SR               REG32(STM32_ADC1_BASE + 0x00)
#define STM32_ADC_CR1              REG32(STM32_ADC1_BASE + 0x04)
#define STM32_ADC_CR2              REG32(STM32_ADC1_BASE + 0x08)
#define STM32_ADC_SMPR1            REG32(STM32_ADC1_BASE + 0x0C)
#define STM32_ADC_SMPR2            REG32(STM32_ADC1_BASE + 0x10)
#define STM32_ADC_SMPR3            REG32(STM32_ADC1_BASE + 0x14)
#define STM32_ADC_JOFR1            REG32(STM32_ADC1_BASE + 0x18)
#define STM32_ADC_JOFR2            REG32(STM32_ADC1_BASE + 0x1C)
#define STM32_ADC_JOFR3            REG32(STM32_ADC1_BASE + 0x20)
#define STM32_ADC_JOFR4            REG32(STM32_ADC1_BASE + 0x24)
#define STM32_ADC_HTR              REG32(STM32_ADC1_BASE + 0x28)
#define STM32_ADC_LTR              REG32(STM32_ADC1_BASE + 0x2C)
#define STM32_ADC_SQR(n)           REG32(STM32_ADC1_BASE + 0x2C + (n) * 4)
#define STM32_ADC_SQR1             REG32(STM32_ADC1_BASE + 0x30)
#define STM32_ADC_SQR2             REG32(STM32_ADC1_BASE + 0x34)
#define STM32_ADC_SQR3             REG32(STM32_ADC1_BASE + 0x38)
#define STM32_ADC_SQR4             REG32(STM32_ADC1_BASE + 0x3C)
#define STM32_ADC_SQR5             REG32(STM32_ADC1_BASE + 0x40)
#define STM32_ADC_JSQR             REG32(STM32_ADC1_BASE + 0x44)
#define STM32_ADC_JDR1             REG32(STM32_ADC1_BASE + 0x48)
#define STM32_ADC_JDR2             REG32(STM32_ADC1_BASE + 0x4C)
#define STM32_ADC_JDR3             REG32(STM32_ADC1_BASE + 0x50)
#define STM32_ADC_JDR3             REG32(STM32_ADC1_BASE + 0x50)
#define STM32_ADC_JDR4             REG32(STM32_ADC1_BASE + 0x54)
#define STM32_ADC_DR               REG32(STM32_ADC1_BASE + 0x58)
#define STM32_ADC_SMPR0            REG32(STM32_ADC1_BASE + 0x5C)

#define STM32_ADC_CCR              REG32(STM32_ADC_BASE + 0x04)
#endif

/* --- Comparators --- */
#if defined(CHIP_FAMILY_STM32L)
#define STM32_COMP_CSR              REG32(STM32_COMP_BASE + 0x00)

#define STM32_COMP_OUTSEL_TIM2_IC4  (0 << 21)
#define STM32_COMP_OUTSEL_TIM2_OCR  (1 << 21)
#define STM32_COMP_OUTSEL_TIM3_IC4  (2 << 21)
#define STM32_COMP_OUTSEL_TIM3_OCR  (3 << 21)
#define STM32_COMP_OUTSEL_TIM4_IC4  (4 << 21)
#define STM32_COMP_OUTSEL_TIM4_OCR  (5 << 21)
#define STM32_COMP_OUTSEL_TIM10_IC1 (6 << 21)
#define STM32_COMP_OUTSEL_NONE      (7 << 21)

#define STM32_COMP_INSEL_NONE       (0 << 18)
#define STM32_COMP_INSEL_PB3        (1 << 18)
#define STM32_COMP_INSEL_VREF       (2 << 18)
#define STM32_COMP_INSEL_VREF34     (3 << 18)
#define STM32_COMP_INSEL_VREF12     (4 << 18)
#define STM32_COMP_INSEL_VREF14     (5 << 18)
#define STM32_COMP_INSEL_DAC_OUT1   (6 << 18)
#define STM32_COMP_INSEL_DAC_OUT2   (7 << 18)

#define STM32_COMP_WNDWE            BIT(17)
#define STM32_COMP_VREFOUTEN        BIT(16)
#define STM32_COMP_CMP2OUT          BIT(13)
#define STM32_COMP_SPEED_FAST       BIT(12)

#define STM32_COMP_CMP1OUT          BIT(7)
#define STM32_COMP_CMP1EN           BIT(4)

#define STM32_COMP_400KPD           BIT(3)
#define STM32_COMP_10KPD            BIT(2)
#define STM32_COMP_400KPU           BIT(1)
#define STM32_COMP_10KPU            BIT(0)

#elif defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
#define STM32_COMP_CSR              REG32(STM32_COMP_BASE + 0x1C)

#define STM32_COMP_CMP2LOCK            BIT(31)
#define STM32_COMP_CMP2OUT             BIT(30)
#define STM32_COMP_CMP2HYST_HI         (3 << 28)
#define STM32_COMP_CMP2HYST_MED        (2 << 28)
#define STM32_COMP_CMP2HYST_LOW        (1 << 28)
#define STM32_COMP_CMP2HYST_NO         (0 << 28)
#define STM32_COMP_CMP2POL             BIT(27)

#define STM32_COMP_CMP2OUTSEL_TIM3_OCR (7 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM3_IC1 (6 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM2_OCR (5 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM2_IC4 (4 << 24)
#ifdef CHIP_VARIANT_STM32F373
#define STM32_COMP_CMP2OUTSEL_TIM4_OCR (3 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM4_IC1 (2 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM16_BRK (1 << 24)
#else
#define STM32_COMP_CMP2OUTSEL_TIM1_OCR (3 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM1_IC1 (2 << 24)
#define STM32_COMP_CMP2OUTSEL_TIM1_BRK (1 << 24)
#endif
#define STM32_COMP_CMP2OUTSEL_NONE     (0 << 24)
#define STM32_COMP_WNDWEN              BIT(23)

#define STM32_COMP_CMP2INSEL_MASK      (7 << 20)
#define STM32_COMP_CMP2INSEL_INM7      (6 << 20) /* STM32F373 only */
#define STM32_COMP_CMP2INSEL_INM6      (6 << 20)
#define STM32_COMP_CMP2INSEL_INM5      (5 << 20)
#define STM32_COMP_CMP2INSEL_INM4      (4 << 20)
#define STM32_COMP_CMP2INSEL_VREF      (3 << 20)
#define STM32_COMP_CMP2INSEL_VREF34    (2 << 20)
#define STM32_COMP_CMP2INSEL_VREF12    (1 << 20)
#define STM32_COMP_CMP2INSEL_VREF14    (0 << 20)

#define STM32_COMP_CMP2MODE_VLSPEED    (3 << 18)
#define STM32_COMP_CMP2MODE_LSPEED     (2 << 18)
#define STM32_COMP_CMP2MODE_MSPEED     (1 << 18)
#define STM32_COMP_CMP2MODE_HSPEED     (0 << 18)
#define STM32_COMP_CMP2EN              BIT(16)

#define STM32_COMP_CMP1LOCK            BIT(15)
#define STM32_COMP_CMP1OUT             BIT(14)
#define STM32_COMP_CMP1HYST_HI         (3 << 12)
#define STM32_COMP_CMP1HYST_MED        (2 << 12)
#define STM32_COMP_CMP1HYST_LOW        (1 << 12)
#define STM32_COMP_CMP1HYST_NO         (0 << 12)
#define STM32_COMP_CMP1POL             BIT(11)

#ifdef CHIP_VARIANT_STM32F373
#define STM32_COMP_CMP1OUTSEL_TIM5_OCR (7 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM5_IC4 (6 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM2_OCR (5 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM2_IC4 (4 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM3_OCR (3 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM3_IC1 (2 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM15_BRK (1 << 8)
#else
#define STM32_COMP_CMP1OUTSEL_TIM3_OCR (7 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM3_IC1 (6 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM2_OCR (5 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM2_IC4 (4 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM1_OCR (3 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM1_IC1 (2 << 8)
#define STM32_COMP_CMP1OUTSEL_TIM1_BRK (1 << 8)
#endif
#define STM32_COMP_CMP1OUTSEL_NONE     (0 << 8)

#define STM32_COMP_CMP1INSEL_MASK      (7 << 4)
#define STM32_COMP_CMP1INSEL_INM7      (7 << 4) /* STM32F373 only */
#define STM32_COMP_CMP1INSEL_INM6      (6 << 4)
#define STM32_COMP_CMP1INSEL_INM5      (5 << 4)
#define STM32_COMP_CMP1INSEL_INM4      (4 << 4)
#define STM32_COMP_CMP1INSEL_VREF      (3 << 4)
#define STM32_COMP_CMP1INSEL_VREF34    (2 << 4)
#define STM32_COMP_CMP1INSEL_VREF12    (1 << 4)
#define STM32_COMP_CMP1INSEL_VREF14    (0 << 4)

#define STM32_COMP_CMP1MODE_VLSPEED    (3 << 2)
#define STM32_COMP_CMP1MODE_LSPEED     (2 << 2)
#define STM32_COMP_CMP1MODE_MSPEED     (1 << 2)
#define STM32_COMP_CMP1MODE_HSPEED     (0 << 2)
#define STM32_COMP_CMP1SW1             BIT(1)
#define STM32_COMP_CMP1EN              BIT(0)
#endif
/* --- Routing interface --- */
/* STM32L1xx only */
#define STM32_RI_ICR                REG32(STM32_COMP_BASE + 0x04)
#define STM32_RI_ASCR1              REG32(STM32_COMP_BASE + 0x08)
#define STM32_RI_ASCR2              REG32(STM32_COMP_BASE + 0x0C)
#define STM32_RI_HYSCR1             REG32(STM32_COMP_BASE + 0x10)
#define STM32_RI_HYSCR2             REG32(STM32_COMP_BASE + 0x14)
#define STM32_RI_HYSCR3             REG32(STM32_COMP_BASE + 0x18)
#define STM32_RI_AMSR1              REG32(STM32_COMP_BASE + 0x1C)
#define STM32_RI_CMR1               REG32(STM32_COMP_BASE + 0x20)
#define STM32_RI_CICR1              REG32(STM32_COMP_BASE + 0x24)
#define STM32_RI_AMSR2              REG32(STM32_COMP_BASE + 0x28)
#define STM32_RI_CMR2               REG32(STM32_COMP_BASE + 0x30)
#define STM32_RI_CICR2              REG32(STM32_COMP_BASE + 0x34)
#define STM32_RI_AMSR3              REG32(STM32_COMP_BASE + 0x38)
#define STM32_RI_CMR3               REG32(STM32_COMP_BASE + 0x3C)
#define STM32_RI_CICR3              REG32(STM32_COMP_BASE + 0x40)
#define STM32_RI_AMSR4              REG32(STM32_COMP_BASE + 0x44)
#define STM32_RI_CMR4               REG32(STM32_COMP_BASE + 0x48)
#define STM32_RI_CICR4              REG32(STM32_COMP_BASE + 0x4C)
#define STM32_RI_AMSR5              REG32(STM32_COMP_BASE + 0x50)
#define STM32_RI_CMR5               REG32(STM32_COMP_BASE + 0x54)
#define STM32_RI_CICR5              REG32(STM32_COMP_BASE + 0x58)

/* --- DAC --- */
#define STM32_DAC_CR               REG32(STM32_DAC_BASE + 0x00)
#define STM32_DAC_SWTRIGR          REG32(STM32_DAC_BASE + 0x04)
#define STM32_DAC_DHR12R1          REG32(STM32_DAC_BASE + 0x08)
#define STM32_DAC_DHR12L1          REG32(STM32_DAC_BASE + 0x0C)
#define STM32_DAC_DHR8R1           REG32(STM32_DAC_BASE + 0x10)
#define STM32_DAC_DHR12R2          REG32(STM32_DAC_BASE + 0x14)
#define STM32_DAC_DHR12L2          REG32(STM32_DAC_BASE + 0x18)
#define STM32_DAC_DHR8R2           REG32(STM32_DAC_BASE + 0x1C)
#define STM32_DAC_DHR12RD          REG32(STM32_DAC_BASE + 0x20)
#define STM32_DAC_DHR12LD          REG32(STM32_DAC_BASE + 0x24)
#define STM32_DAC_DHR8RD           REG32(STM32_DAC_BASE + 0x28)
#define STM32_DAC_DOR1             REG32(STM32_DAC_BASE + 0x2C)
#define STM32_DAC_DOR2             REG32(STM32_DAC_BASE + 0x30)
#define STM32_DAC_SR               REG32(STM32_DAC_BASE + 0x34)

#define STM32_DAC_CR_DMAEN2        BIT(28)
#define STM32_DAC_CR_TSEL2_SWTRG   (7 << 19)
#define STM32_DAC_CR_TSEL2_TMR4    (5 << 19)
#define STM32_DAC_CR_TSEL2_TMR2    (4 << 19)
#define STM32_DAC_CR_TSEL2_TMR9    (3 << 19)
#define STM32_DAC_CR_TSEL2_TMR7    (2 << 19)
#define STM32_DAC_CR_TSEL2_TMR6    (0 << 19)
#define STM32_DAC_CR_TSEL2_MASK    (7 << 19)
#define STM32_DAC_CR_TEN2          BIT(18)
#define STM32_DAC_CR_BOFF2         BIT(17)
#define STM32_DAC_CR_EN2           BIT(16)
#define STM32_DAC_CR_DMAEN1        BIT(12)
#define STM32_DAC_CR_TSEL1_SWTRG   (7 << 3)
#define STM32_DAC_CR_TSEL1_TMR4    (5 << 3)
#define STM32_DAC_CR_TSEL1_TMR2    (4 << 3)
#define STM32_DAC_CR_TSEL1_TMR9    (3 << 3)
#define STM32_DAC_CR_TSEL1_TMR7    (2 << 3)
#define STM32_DAC_CR_TSEL1_TMR6    (0 << 3)
#define STM32_DAC_CR_TSEL1_MASK    (7 << 3)
#define STM32_DAC_CR_TEN1          BIT(2)
#define STM32_DAC_CR_BOFF1         BIT(1)
#define STM32_DAC_CR_EN1           BIT(0)

/* --- DMA --- */

#if defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32H7)

/*
 * Available DMA streams, numbered from 0.
 *
 * Named channel to respect older interface, but a stream can serve
 * any channels, as long as they are in the same DMA controller.
 *
 * Stream 0 - 7 are managed by controller DMA1, 8 - 15 DMA2.
 */
enum dma_channel {
	/* Channel numbers */
	STM32_DMA1_STREAM0 = 0,
	STM32_DMA1_STREAM1 = 1,
	STM32_DMA1_STREAM2 = 2,
	STM32_DMA1_STREAM3 = 3,
	STM32_DMA1_STREAM4 = 4,
	STM32_DMA1_STREAM5 = 5,
	STM32_DMA1_STREAM6 = 6,
	STM32_DMA1_STREAM7 = 7,
	STM32_DMAS_COUNT = 8,
	STM32_DMA2_STREAM0 = 8,
	STM32_DMA2_STREAM1 = 9,
	STM32_DMA2_STREAM2 = 10,
	STM32_DMA2_STREAM3 = 11,
	STM32_DMA2_STREAM4 = 12,
	STM32_DMA2_STREAM5 = 13,
	STM32_DMA2_STREAM6 = 14,
	STM32_DMA2_STREAM7 = 15,

	STM32_DMAS_USART1_TX = STM32_DMA2_STREAM7,
	STM32_DMAS_USART1_RX = STM32_DMA2_STREAM5,

	/* Legacy naming for uart.c */
	STM32_DMAC_USART1_TX = STM32_DMAS_USART1_TX,
	STM32_DMAC_USART1_RX = STM32_DMAS_USART1_RX,
#if defined(CHIP_VARIANT_STM32F41X) || defined(CHIP_VARIANT_STM32F76X)
	STM32_DMAS_USART2_TX = STM32_DMA1_STREAM6,
	STM32_DMAS_USART2_RX = STM32_DMA1_STREAM5,

	/* Legacy naming for uart.c */
	STM32_DMAC_USART2_TX = STM32_DMAS_USART2_TX,
	STM32_DMAC_USART2_RX = STM32_DMAS_USART2_RX,
#endif

#if defined(CHIP_VARIANT_STM32F41X) || defined(CHIP_VARIANT_STM32F76X)
	STM32_DMAC_I2C1_TX = STM32_DMA1_STREAM1,
	STM32_DMAC_I2C1_RX = STM32_DMA1_STREAM0,

	STM32_DMAC_I2C2_TX = STM32_DMA1_STREAM7,
	STM32_DMAC_I2C2_RX = STM32_DMA1_STREAM3,

	STM32_DMAC_I2C3_TX = STM32_DMA1_STREAM4,
	STM32_DMAC_I2C3_RX = STM32_DMA1_STREAM2,
#else
	STM32_DMAC_I2C1_TX = STM32_DMA1_STREAM6,
	STM32_DMAC_I2C1_RX = STM32_DMA1_STREAM0,

	STM32_DMAC_I2C2_TX = STM32_DMA1_STREAM7,
	STM32_DMAC_I2C2_RX = STM32_DMA1_STREAM3,

	STM32_DMAC_I2C3_TX = STM32_DMA1_STREAM4,
	STM32_DMAC_I2C3_RX = STM32_DMA1_STREAM1,
#endif

	STM32_DMAC_FMPI2C4_TX = STM32_DMA1_STREAM5,
	STM32_DMAC_FMPI2C4_RX = STM32_DMA1_STREAM2,

	/* Legacy naming for spi_master.c */
	STM32_DMAC_SPI1_TX = STM32_DMA2_STREAM3, /* REQ 3 */
	STM32_DMAC_SPI1_RX = STM32_DMA2_STREAM0, /* REQ 3 */
	STM32_DMAC_SPI2_TX = STM32_DMA1_STREAM4, /* REQ 0 */
	STM32_DMAC_SPI2_RX = STM32_DMA1_STREAM3, /* REQ 0 */
	STM32_DMAC_SPI3_TX = STM32_DMA1_STREAM7, /* REQ 0 */
	STM32_DMAC_SPI3_RX = STM32_DMA1_STREAM0, /* REQ 0 */
	STM32_DMAC_SPI4_TX = STM32_DMA2_STREAM1, /* STM32H7 */
	STM32_DMAC_SPI4_RX = STM32_DMA2_STREAM4, /* STM32H7 */
};

#define STM32_REQ_USART1_TX 4
#define STM32_REQ_USART1_RX 4

#define STM32_REQ_USART2_TX 4
#define STM32_REQ_USART2_RX 4

#define STM32_I2C1_TX_REQ_CH 1
#define STM32_I2C1_RX_REQ_CH 1

#define STM32_I2C2_TX_REQ_CH 7
#define STM32_I2C2_RX_REQ_CH 7

#define STM32_I2C3_TX_REQ_CH 3
#define STM32_I2C3_RX_REQ_CH 1

#define STM32_FMPI2C4_TX_REQ_CH 2
#define STM32_FMPI2C4_RX_REQ_CH 2

#define STM32_SPI1_TX_REQ_CH 3
#define STM32_SPI1_RX_REQ_CH 3
#define STM32_SPI2_TX_REQ_CH 0
#define STM32_SPI2_RX_REQ_CH 0
#define STM32_SPI3_TX_REQ_CH 0
#define STM32_SPI3_RX_REQ_CH 0

#define STM32_DMAS_TOTAL_COUNT 16

/* Registers for a single stream of a DMA controller */
struct stm32_dma_stream {
	uint32_t	scr;		/* Control */
	uint32_t	sndtr;		/* Number of data to transfer */
	uint32_t	spar;		/* Peripheral address */
	uint32_t	sm0ar;		/* Memory address 0 */
	uint32_t	sm1ar;		/*  address 1 for double buffer */
	uint32_t	sfcr;		/* FIFO control */
};

/* Always use stm32_dma_stream_t so volatile keyword is included! */
typedef volatile struct stm32_dma_stream stm32_dma_stream_t;

/* Common code and header file must use this */
typedef stm32_dma_stream_t dma_chan_t;
struct stm32_dma_regs {
	uint32_t	isr[2];
	uint32_t	ifcr[2];
	stm32_dma_stream_t stream[STM32_DMAS_COUNT];
};

#else  /* CHIP_FAMILY_STM32F4 */

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
#if !defined(CHIP_VARIANT_STM32F03X) && !defined(CHIP_VARIANT_STM32F05X)
	STM32_DMAC_USART2_RX = STM32_DMAC_CH6,
	STM32_DMAC_USART2_TX = STM32_DMAC_CH7,
	STM32_DMAC_I2C1_TX = STM32_DMAC_CH6,
	STM32_DMAC_I2C1_RX = STM32_DMAC_CH7,
	STM32_DMAC_PMSE_ROW = STM32_DMAC_CH6,
	STM32_DMAC_PMSE_COL = STM32_DMAC_CH7,
#ifdef CHIP_FAMILY_STM32L4
	STM32_DMAC_SPI2_RX = STM32_DMAC_CH4,
	STM32_DMAC_SPI2_TX = STM32_DMAC_CH5,
	STM32_DMAC_SPI3_RX = STM32_DMAC_CH9,
	STM32_DMAC_SPI3_TX = STM32_DMAC_CH10,
	STM32_DMAC_COUNT = 14,
#elif defined(CHIP_VARIANT_STM32F373)
	STM32_DMAC_SPI2_RX = STM32_DMAC_CH4,
	STM32_DMAC_SPI2_TX = STM32_DMAC_CH5,
	STM32_DMAC_SPI3_RX = STM32_DMAC_CH9,
	STM32_DMAC_SPI3_TX = STM32_DMAC_CH10,

	STM32_DMAC_COUNT = 10,
#else
	STM32_DMAC_SPI2_RX = STM32_DMAC_CH6,
	STM32_DMAC_SPI2_TX = STM32_DMAC_CH7,

	/* Only DMA1 (with 7 channels) is present on STM32L151x */
	STM32_DMAC_COUNT = 7,
#endif

#else /* stm32f03x and stm32f05x have only 5 channels */
	STM32_DMAC_COUNT = 5,
#endif
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
#endif /* CHIP_FAMILY_STM32F4 */

/* Always use stm32_dma_regs_t so volatile keyword is included! */
typedef volatile struct stm32_dma_regs stm32_dma_regs_t;

#define STM32_DMA1_REGS ((stm32_dma_regs_t *)STM32_DMA1_BASE)



#if defined(CHIP_FAMILY_STM32F4) || defined(CHIP_FAMILY_STM32H7)
#define STM32_DMA2_REGS ((stm32_dma_regs_t *)STM32_DMA2_BASE)

#define STM32_DMA_REGS(channel) \
	((channel) < STM32_DMAS_COUNT ? STM32_DMA1_REGS : STM32_DMA2_REGS)

#define STM32_DMA_CCR_EN                BIT(0)
#define STM32_DMA_CCR_DMEIE             BIT(1)
#define STM32_DMA_CCR_TEIE              BIT(2)
#define STM32_DMA_CCR_HTIE              BIT(3)
#define STM32_DMA_CCR_TCIE              BIT(4)
#define STM32_DMA_CCR_PFCTRL            BIT(5)
#define STM32_DMA_CCR_DIR_P2M		(0 << 6)
#define STM32_DMA_CCR_DIR_M2P		(1 << 6)
#define STM32_DMA_CCR_DIR_M2M		(2 << 6)
#define STM32_DMA_CCR_CIRC              BIT(8)
#define STM32_DMA_CCR_PINC              BIT(9)
#define STM32_DMA_CCR_MINC              BIT(10)
#define STM32_DMA_CCR_PSIZE_8_BIT       (0 << 11)
#define STM32_DMA_CCR_PSIZE_16_BIT      (1 << 11)
#define STM32_DMA_CCR_PSIZE_32_BIT      (2 << 11)
#define STM32_DMA_CCR_MSIZE_8_BIT       (0 << 13)
#define STM32_DMA_CCR_MSIZE_16_BIT      (1 << 13)
#define STM32_DMA_CCR_MSIZE_32_BIT      (2 << 13)
#define STM32_DMA_CCR_PINCOS            BIT(15)
#define STM32_DMA_CCR_PL_LOW            (0 << 16)
#define STM32_DMA_CCR_PL_MEDIUM         (1 << 16)
#define STM32_DMA_CCR_PL_HIGH           (2 << 16)
#define STM32_DMA_CCR_PL_VERY_HIGH      (3 << 16)
#define STM32_DMA_CCR_DBM               BIT(18)
#define STM32_DMA_CCR_CT                BIT(19)
#define STM32_DMA_CCR_PBURST(b_len)		 ((((b_len) - 4) / 4) << 21)
#define STM32_DMA_CCR_MBURST(b_len)		 ((((b_len) - 4) / 4) << 21)
#ifdef CHIP_FAMILY_STM32H7
#define STM32_DMA_CCR_CHANNEL_MASK		 (0 << 25)
#define STM32_DMA_CCR_CHANNEL(channel)		 (0)
#else  /* !CHIP_FAMILY_STM32H7 */
#define STM32_DMA_CCR_CHANNEL_MASK		 (0x7 << 25)
#define STM32_DMA_CCR_CHANNEL(channel)		 ((channel) << 25)
#endif /* !CHIP_FAMILY_STM32H7 */
#define STM32_DMA_CCR_RSVD_MASK		(0xF0100000)


#define STM32_DMA_SFCR_DMDIS		BIT(2)
#define STM32_DMA_SFCR_FTH(level)	(((level) - 1) << 0)


#define STM32_DMA_CH_LOCAL(channel)     ((channel) % STM32_DMAS_COUNT)
#define STM32_DMA_CH_LH(channel)        \
	((STM32_DMA_CH_LOCAL(channel) < 4) ? 0 : 1)
#define STM32_DMA_CH_OFFSET(channel)    \
	(((STM32_DMA_CH_LOCAL(channel) % 4) * 6) + \
	(((STM32_DMA_CH_LOCAL(channel) % 4) >= 2) ? 4 : 0))
#define STM32_DMA_CH_GETBITS(channel, val) \
	(((val) >> STM32_DMA_CH_OFFSET(channel)) & 0x3f)
#define STM32_DMA_GET_IFCR(channel)      \
	(STM32_DMA_CH_GETBITS(channel,   \
	STM32_DMA_REGS(channel)->ifcr[STM32_DMA_CH_LH(channel)]))
#define STM32_DMA_GET_ISR(channel)       \
	(STM32_DMA_CH_GETBITS(channel,   \
	STM32_DMA_REGS(channel)->isr[STM32_DMA_CH_LH(channel)]))

#define STM32_DMA_SET_IFCR(channel, val) \
	(STM32_DMA_REGS(channel)->ifcr[STM32_DMA_CH_LH(channel)] = \
	(STM32_DMA_REGS(channel)->ifcr[STM32_DMA_CH_LH(channel)] & \
	~(0x3f << STM32_DMA_CH_OFFSET(channel))) | \
	(((val) & 0x3f) << STM32_DMA_CH_OFFSET(channel)))
#define STM32_DMA_SET_ISR(channel, val) \
	(STM32_DMA_REGS(channel)->isr[STM32_DMA_CH_LH(channel)] = \
	(STM32_DMA_REGS(channel)->isr[STM32_DMA_CH_LH(channel)] & \
	~(0x3f << STM32_DMA_CH_OFFSET(channel))) | \
	(((val) & 0x3f) << STM32_DMA_CH_OFFSET(channel)))

#define STM32_DMA_FEIF                  BIT(0)
#define STM32_DMA_DMEIF                 BIT(2)
#define STM32_DMA_TEIF                  BIT(3)
#define STM32_DMA_HTIF                  BIT(4)
#define STM32_DMA_TCIF                  BIT(5)
#define STM32_DMA_ALL                   0x3d

#else /* !CHIP_FAMILY_STM32F4 */
#define STM32_DMA_CCR_CHANNEL(channel)		 (0)

#if defined(CHIP_FAMILY_STM32F3) || defined(CHIP_FAMILY_STM32L4) || \
	defined(CHIP_VARIANT_STM32F09X)
#define STM32_DMA2_REGS ((stm32_dma_regs_t *)STM32_DMA2_BASE)
#define STM32_DMA_REGS(channel) \
	((channel) < STM32_DMAC_PER_CTLR ? STM32_DMA1_REGS : STM32_DMA2_REGS)
#define STM32_DMA_CSELR(channel) \
	REG32(((channel) < STM32_DMAC_PER_CTLR ? \
			STM32_DMA1_BASE : STM32_DMA2_BASE)  + 0xA8)
#else
#define STM32_DMA_REGS(channel) STM32_DMA1_REGS
#endif

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
#endif /* !CHIP_FAMILY_STM32F4 */

#ifdef CHIP_FAMILY_STM32H7
/* The requests for the DMA1/DMA2 controllers are routed through DMAMUX1. */
/* DMAMUX1/2 registers */
#define DMAMUX1 0
#define DMAMUX2 1
#define STM32_DMAMUX_BASE(n)        ((n) ? STM32_DMAMUX2_BASE \
					 : STM32_DMAMUX1_BASE)
#define STM32_DMAMUX_REG32(n, off)  REG32(STM32_DMAMUX_BASE(n) + (off))
#define STM2_DMAMUX_CxCR(n, x)      STM32_DMAMUX_REG32(n, 4 * (x))
#define STM2_DMAMUX_CSR(n)          STM32_DMAMUX_REG32(n, 0x80)
#define STM2_DMAMUX_CFR(n)          STM32_DMAMUX_REG32(n, 0x84)
#define STM2_DMAMUX_RGxCR(n, x)     STM32_DMAMUX_REG32(n, 0x100 + 4 * (x))
#define STM2_DMAMUX_RGSR(n)         STM32_DMAMUX_REG32(n, 0x140)
#define STM2_DMAMUX_RGCFR(n)        STM32_DMAMUX_REG32(n, 0x144)

enum dmamux1_request {
	DMAMUX1_REQ_ADC1 = 9,
	DMAMUX1_REQ_ADC2 = 10,
	DMAMUX1_REQ_TIM1_CH1 = 11,
	DMAMUX1_REQ_TIM1_CH2 = 12,
	DMAMUX1_REQ_TIM1_CH3 = 13,
	DMAMUX1_REQ_TIM1_CH4 = 14,
	DMAMUX1_REQ_TIM1_UP = 15,
	DMAMUX1_REQ_TIM1_TRIG = 16,
	DMAMUX1_REQ_TIM1_COM = 17,
	DMAMUX1_REQ_TIM2_CH1 = 18,
	DMAMUX1_REQ_TIM2_CH2 = 19,
	DMAMUX1_REQ_TIM2_CH3 = 20,
	DMAMUX1_REQ_TIM2_CH4 = 21,
	DMAMUX1_REQ_TIM2_UP = 22,
	DMAMUX1_REQ_TIM3_CH1 = 23,
	DMAMUX1_REQ_TIM3_CH2 = 24,
	DMAMUX1_REQ_TIM3_CH3 = 25,
	DMAMUX1_REQ_TIM3_CH4 = 26,
	DMAMUX1_REQ_TIM3_UP = 27,
	DMAMUX1_REQ_TIM3_TRIG = 28,
	DMAMUX1_REQ_TIM4_CH1 = 29,
	DMAMUX1_REQ_TIM4_CH2 = 30,
	DMAMUX1_REQ_TIM4_CH3 = 31,
	DMAMUX1_REQ_TIM4_UP = 32,
	DMAMUX1_REQ_I2C1_RX = 33,
	DMAMUX1_REQ_I2C1_TX = 34,
	DMAMUX1_REQ_I2C2_RX = 35,
	DMAMUX1_REQ_I2C2_TX = 36,
	DMAMUX1_REQ_SPI1_RX = 37,
	DMAMUX1_REQ_SPI1_TX = 38,
	DMAMUX1_REQ_SPI2_RX = 39,
	DMAMUX1_REQ_SPI2_TX = 40,
	DMAMUX1_REQ_USART1_RX = 41,
	DMAMUX1_REQ_USART1_TX = 42,
	DMAMUX1_REQ_USART2_RX = 43,
	DMAMUX1_REQ_USART2_TX = 44,
	DMAMUX1_REQ_USART3_RX = 45,
	DMAMUX1_REQ_USART3_TX = 46,
	DMAMUX1_REQ_TIM8_CH1 = 47,
	DMAMUX1_REQ_TIM8_CH2 = 48,
	DMAMUX1_REQ_TIM8_CH3 = 49,
	DMAMUX1_REQ_TIM8_CH4 = 50,
	DMAMUX1_REQ_TIM8_UP = 51,
	DMAMUX1_REQ_TIM8_TRIG = 52,
	DMAMUX1_REQ_TIM8_COM = 53,
	DMAMUX1_REQ_TIM5_CH1 = 55,
	DMAMUX1_REQ_TIM5_CH2 = 56,
	DMAMUX1_REQ_TIM5_CH3 = 57,
	DMAMUX1_REQ_TIM5_CH4 = 58,
	DMAMUX1_REQ_TIM5_UP = 59,
	DMAMUX1_REQ_TIM5_TRIG = 60,
	DMAMUX1_REQ_SPI3_RX = 61,
	DMAMUX1_REQ_SPI3_TX = 62,
	DMAMUX1_REQ_UART4_RX = 63,
	DMAMUX1_REQ_UART4_TX = 64,
	DMAMUX1_REQ_USART5_RX = 65,
	DMAMUX1_REQ_UART5_TX = 66,
	DMAMUX1_REQ_DAC1 = 67,
	DMAMUX1_REQ_DAC2 = 68,
	DMAMUX1_REQ_TIM6_UP = 69,
	DMAMUX1_REQ_TIM7_UP = 70,
	DMAMUX1_REQ_USART6_RX = 71,
	DMAMUX1_REQ_USART6_TX = 72,
	DMAMUX1_REQ_I2C3_RX = 73,
	DMAMUX1_REQ_I2C3_TX = 74,
	DMAMUX1_REQ_DCMI = 75,
	DMAMUX1_REQ_CRYP_IN = 76,
	DMAMUX1_REQ_CRYP_OUT = 77,
	DMAMUX1_REQ_HASH_IN = 78,
	DMAMUX1_REQ_UART7_RX = 79,
	DMAMUX1_REQ_UART7_TX = 80,
	DMAMUX1_REQ_UART8_RX = 81,
	DMAMUX1_REQ_UART8_TX = 82,
	DMAMUX1_REQ_SPI4_RX = 83,
	DMAMUX1_REQ_SPI4_TX = 84,
	DMAMUX1_REQ_SPI5_RX = 85,
	DMAMUX1_REQ_SPI5_TX = 86,
	DMAMUX1_REQ_SAI1_A = 87,
	DMAMUX1_REQ_SAI1_B = 88,
	DMAMUX1_REQ_SAI2_A = 89,
	DMAMUX1_REQ_SAI2_B = 90,
	DMAMUX1_REQ_SWPMI_RX = 91,
	DMAMUX1_REQ_SWPMI_TX = 92,
	DMAMUX1_REQ_SPDIFRX_DT = 93,
	DMAMUX1_REQ_SPDIFRX_CS = 94,
	DMAMUX1_REQ_TIM15_CH1 = 105,
	DMAMUX1_REQ_TIM15_UP = 106,
	DMAMUX1_REQ_TIM15_TRIG = 107,
	DMAMUX1_REQ_TIM15_COM = 108,
	DMAMUX1_REQ_TIM16_CH1 = 109,
	DMAMUX1_REQ_TIM16_UP = 110,
	DMAMUX1_REQ_TIM17_CH1 = 111,
	DMAMUX1_REQ_TIM17_UP = 112,
	DMAMUX1_REQ_SAI3_A = 113,
	DMAMUX1_REQ_SAI3_B = 114,
	DMAMUX1_REQ_ADC3 = 115,
};
#endif /* CHIP_FAMILY_STM32H7 */

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

#endif /* __CROS_EC_REGISTERS_H */
