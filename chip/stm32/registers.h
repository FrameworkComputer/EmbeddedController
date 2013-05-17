/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for STM32 processor
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"

/* concatenation helper */
#define STM32_CAT(prefix, n, suffix) prefix ## n ## suffix

/* IRQ numbers */
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
#define STM32_IRQ_ADC_1           18 /* ADC1 and ADC2 interrupt on STM32F10x */
#define STM32_IRQ_USB_HP          19
#define STM32_IRQ_CAN_TX          19 /* STM32F10x only */
#define STM32_IRQ_USB_LP          20
#define STM32_IRQ_USB_LP_CAN_RX   20 /* STM32F10x only */
#define STM32_IRQ_DAC             21
#define STM32_IRQ_CAN_RX1         21 /* STM32F10x only */
#define STM32_IRQ_COMP            22
#define STM32_IRQ_CAN_SCE         22 /* STM32F10x only */
#define STM32_IRQ_EXTI9_5         23
#define STM32_IRQ_LCD             24 /* STM32L15X only */
#define STM32_IRQ_TIM1_BRK_TIM15  24 /* TIM15 interrupt on STM32F100 only */
#define STM32_IRQ_TIM9            25 /* STM32L15X only */
#define STM32_IRQ_TIM1_UP_TIM16   25 /* TIM16 interrupt on STM32F100 only */
#define STM32_IRQ_TIM10           26 /* STM32L15X only */
#define STM32_IRQ_TIM1_TRG_TIM17  26 /* STM32F100 only */
#define STM32_IRQ_TIM1_TRG_COM    26 /* STM32F10x only */
#define STM32_IRQ_TIM11           27 /* STM32L15X only */
#define STM32_IRQ_TIM1_CC         27 /* STM32F100 and STM32F10x */
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
#define STM32_IRQ_USB_FS_WAKEUP   42 /* STM32L15X and STM32F10x */
#define STM32_IRQ_CEC             42 /* STM32F100 only */
#define STM32_IRQ_TIM6_BASIC      43 /* STM32L15X only */
#define STM32_IRQ_TIM12           43 /* STM32F100 only */
#define STM32_IRQ_TIM8_BRK        43 /* STM32F10x only */
#define STM32_IRQ_TIM7_BASIC      44 /* STM32L15X only */
#define STM32_IRQ_TIM13           44 /* STM32F100 only */
#define STM32_IRQ_TIM8_UP         44 /* STM32F10x only */
#define STM32_IRQ_TIM14           45 /* STM32F100 only */
#define STM32_IRQ_TIM8_TRG_COM    45 /* STM32F10x only */
#define STM32_IRQ_TIM8_CC         46 /* STM32F10x only */
#define STM32_IRQ_ADC3            47 /* STM32F10x only */
#define STM32_IRQ_FSMC            48 /* STM32F100 and STM32F10x */
#define STM32_IRQ_SDIO            49 /* STM32F10x only */
#define STM32_IRQ_TIM5            50 /* STM32F100 and STM32F10x */
#define STM32_IRQ_SPI3            51 /* STM32F100 and STM32F10x */
#define STM32_IRQ_UART4           52 /* STM32F100 and STM32F10x */
#define STM32_IRQ_UART5           53 /* STM32F100 and STM32F10x */
#define STM32_IRQ_TIM6_DAC        54 /* STM32F100 only */
#define STM32_IRQ_TIM6            54 /* STM32F10x only */
#define STM32_IRQ_TIM7            55 /* STM32F100 and STM32F10x */
#define STM32_IRQ_DMA2_CHANNEL1   56 /* STM32F100 and STM32F10x */
#define STM32_IRQ_DMA2_CHANNEL2   57 /* STM32F100 and STM32F10x */
#define STM32_IRQ_DMA2_CHANNEL3   58 /* STM32F100 and STM32F10x */
#define STM32_IRQ_DMA2_CHANNEL4_5 59 /* STM32F100 and STM32F10x */
/* if MISC_REMAP bits are set */
#define STM32_IRQ_DMA2_CHANNEL5   60 /* STM32F100 only */

/* --- USART --- */
#define STM32_USART1_BASE          0x40013800
#define STM32_USART2_BASE          0x40004400
#define STM32_USART3_BASE          0x40004800
#define STM32_UART4_BASE           0x40004c00 /* STM32F100 and STM32F10x */
#define STM32_UART5_BASE           0x40005000 /* STM32F100 and STM32F10x */

#define STM32_USART_BASE(n)        STM32_CAT(STM32_USART, n, _BASE)

#define STM32_USART_REG(n, offset) \
		REG16(STM32_CAT(STM32_USART, n, _BASE) + (offset))

#define STM32_USART_SR(n)          STM32_USART_REG(n, 0x00)
#define STM32_USART_DR(n)          STM32_USART_REG(n, 0x04)
#define STM32_USART_BRR(n)         STM32_USART_REG(n, 0x08)
#define STM32_USART_CR1(n)         STM32_USART_REG(n, 0x0C)
#define STM32_USART_CR2(n)         STM32_USART_REG(n, 0x10)
#define STM32_USART_CR3(n)         STM32_USART_REG(n, 0x14)
#define STM32_USART_GTPR(n)        STM32_USART_REG(n, 0x18)

#define STM32_IRQ_USART(n)         STM32_CAT(STM32_IRQ_USART, n, )

/* --- TIMERS --- */
#define STM32_TIM1_BASE            0x40012c00 /* STM32F100 and STM32F10x */
#define STM32_TIM2_BASE            0x40000000
#define STM32_TIM3_BASE            0x40000400
#define STM32_TIM4_BASE            0x40000800
#define STM32_TIM5_BASE            0x40000c00 /* STM32F100 and STM32F10x */
#define STM32_TIM6_BASE            0x40001000
#define STM32_TIM7_BASE            0x40001400
#define STM32_TIM8_BASE            0x40013400 /* STM32F10x only */
#if defined(CHIP_VARIANT_stm32l15x)
#define STM32_TIM9_BASE            0x40010800 /* STM32L15X only */
#define STM32_TIM10_BASE           0x40010C00 /* STM32L15X only */
#define STM32_TIM11_BASE           0x40011000 /* STM32L15X only */
#elif defined(CHIP_VARIANT_stm32f10x)
#define STM32_TIM9_BASE            0x40014C00 /* STM32F10x only */
#define STM32_TIM10_BASE           0x40015000 /* STM32F10x only */
#define STM32_TIM11_BASE           0x40015400 /* STM32F10x only */
#endif	/* TIM9-11 */
#define STM32_TIM12_BASE           0x40001800 /* STM32F100 and STM32F10x */
#define STM32_TIM13_BASE           0x40001c00 /* STM32F100 and STM32F10x */
#define STM32_TIM14_BASE           0x40002000 /* STM32F100 and STM32F10x */
#define STM32_TIM15_BASE           0x40014000 /* STM32F100 only */
#define STM32_TIM16_BASE           0x40014400 /* STM32F100 only */
#define STM32_TIM17_BASE           0x40014800 /* STM32F100 only */

#define STM32_TIM_REG(n, offset) \
		REG16(STM32_CAT(STM32_TIM, n, _BASE) + (offset))

#define STM32_TIM_CR1(n)           STM32_TIM_REG(n, 0x00)
#define STM32_TIM_CR2(n)           STM32_TIM_REG(n, 0x04)
#define STM32_TIM_SMCR(n)          STM32_TIM_REG(n, 0x08)
#define STM32_TIM_DIER(n)          STM32_TIM_REG(n, 0x0C)
#define STM32_TIM_SR(n)            STM32_TIM_REG(n, 0x10)
#define STM32_TIM_EGR(n)           STM32_TIM_REG(n, 0x14)
#define STM32_TIM_CCMR1(n)         STM32_TIM_REG(n, 0x18)
#define STM32_TIM_CCMR2(n)         STM32_TIM_REG(n, 0x1C)
#define STM32_TIM_CCER(n)          STM32_TIM_REG(n, 0x20)
#define STM32_TIM_CNT(n)           STM32_TIM_REG(n, 0x24)
#define STM32_TIM_PSC(n)           STM32_TIM_REG(n, 0x28)
#define STM32_TIM_ARR(n)           STM32_TIM_REG(n, 0x2C)
#define STM32_TIM_RCR(n)           STM32_TIM_REG(n, 0x30)
#define STM32_TIM_CCR1(n)          STM32_TIM_REG(n, 0x34)
#define STM32_TIM_CCR2(n)          STM32_TIM_REG(n, 0x38)
#define STM32_TIM_CCR3(n)          STM32_TIM_REG(n, 0x3C)
#define STM32_TIM_CCR4(n)          STM32_TIM_REG(n, 0x40)
#define STM32_TIM_DCR(n)           STM32_TIM_REG(n, 0x48)
#define STM32_TIM_DMAR(n)          STM32_TIM_REG(n, 0x4C)
#define STM32_TIM_OR(n)            STM32_TIM_REG(n, 0x50)

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

	unsigned reserved30;
	unsigned ccr1;
	unsigned ccr2;
	unsigned ccr3;

	unsigned ccr4;
	unsigned reserved44;
	unsigned dcr;
	unsigned dmar;

	unsigned or;
};
/* Must be volatile, or compiler optimizes out repeated accesses */
typedef volatile struct timer_ctlr timer_ctlr_t;

/* --- GPIO --- */

#define GPIO_A                       STM32_GPIOA_BASE
#define GPIO_B                       STM32_GPIOB_BASE
#define GPIO_C                       STM32_GPIOC_BASE
#define GPIO_D                       STM32_GPIOD_BASE
#define GPIO_E                       STM32_GPIOE_BASE
#define GPIO_F                       STM32_GPIOF_BASE
#define GPIO_G                       STM32_GPIOG_BASE
#define GPIO_H                       STM32_GPIOH_BASE

#if defined(CHIP_VARIANT_stm32l15x)
#define STM32_GPIOA_BASE            0x40020000
#define STM32_GPIOB_BASE            0x40020400
#define STM32_GPIOC_BASE            0x40020800
#define STM32_GPIOD_BASE            0x40020C00
#define STM32_GPIOE_BASE            0x40021000
#define STM32_GPIOH_BASE            0x40021400

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
#define GPIO_ALT_USART               0x7
#define GPIO_ALT_USB                 0xA
#define GPIO_ALT_LCD                 0xB
#define GPIO_ALT_RI                  0xE
#define GPIO_ALT_EVENTOUT            0xF

#elif defined(CHIP_VARIANT_stm32f100) || defined(CHIP_VARIANT_stm32f10x)
#define STM32_GPIOA_BASE            0x40010800
#define STM32_GPIOB_BASE            0x40010c00
#define STM32_GPIOC_BASE            0x40011000
#define STM32_GPIOD_BASE            0x40011400
#define STM32_GPIOE_BASE            0x40011800
#define STM32_GPIOF_BASE            0x4001c000
#define STM32_GPIOG_BASE            0x40012000

#define STM32_GPIO_CRL(b)       REG32((b) + 0x00)
#define STM32_GPIO_CRH(b)       REG32((b) + 0x04)
#define STM32_GPIO_IDR(b)       REG16((b) + 0x08)
#define STM32_GPIO_ODR(b)       REG16((b) + 0x0c)
#define STM32_GPIO_BSRR(b)      REG32((b) + 0x10)
#define STM32_GPIO_BRR(b)       REG32((b) + 0x14)
#define STM32_GPIO_LCKR(b)      REG32((b) + 0x18)

#define STM32_AFIO_BASE             0x40010000
#define STM32_AFIO_EXTICR(n)        REG32(STM32_AFIO_BASE + 8 + 4 * (n))
#define STM32_GPIO_AFIO_EVCR        REG32(STM32_AFIO_BASE + 0x00)
#define STM32_GPIO_AFIO_MAPR        REG32(STM32_AFIO_BASE + 0x04)
#define STM32_GPIO_AFIO_MAPR2       REG32(STM32_AFIO_BASE + 0x1c)

#else
#error Unsupported chip variant
#endif




/* --- I2C --- */
#define STM32_I2C1_BASE             0x40005400
#define STM32_I2C2_BASE             0x40005800

#define STM32_I2C1_PORT             0
#define STM32_I2C2_PORT             1

#define stm32_i2c_reg(port, offset) \
	((uint16_t *)((STM32_I2C1_BASE + ((port) * 0x400)) + (offset)))

#define STM32_I2C_CR1(n)            REG16(stm32_i2c_reg(n, 0x00))
#define STM32_I2C_CR1_PE	(1 << 0)
#define STM32_I2C_CR1_START	(1 << 8)
#define STM32_I2C_CR1_STOP	(1 << 9)
#define STM32_I2C_CR1_ACK	(1 << 10)
#define STM32_I2C_CR1_POS	(1 << 11)
#define STM32_I2C_CR1_SWRST	(1 << 15)
#define STM32_I2C_CR2(n)            REG16(stm32_i2c_reg(n, 0x04))
#define STM32_I2C_OAR1(n)           REG16(stm32_i2c_reg(n, 0x08))
#define STM32_I2C_OAR2(n)           REG16(stm32_i2c_reg(n, 0x0C))
#define STM32_I2C_DR(n)             REG16(stm32_i2c_reg(n, 0x10))
#define STM32_I2C_SR1(n)            REG16(stm32_i2c_reg(n, 0x14))
#define STM32_I2C_SR1_SB	(1 << 0)
#define STM32_I2C_SR1_ADDR	(1 << 1)
#define STM32_I2C_SR1_BTF	(1 << 2)
#define STM32_I2C_SR1_RXNE	(1 << 6)
#define STM32_I2C_SR1_TXE	(1 << 7)
#define STM32_I2C_SR1_BERR	(1 << 8)
#define STM32_I2C_SR1_ARLO	(1 << 9)
#define STM32_I2C_SR1_AF	(1 << 10)

#define STM32_I2C_SR2(n)            REG16(stm32_i2c_reg(n, 0x18))
#define STM32_I2C_CCR(n)            REG16(stm32_i2c_reg(n, 0x1C))
#define STM32_I2C_TRISE(n)          REG16(stm32_i2c_reg(n, 0x20))

/* --- Power / Reset / Clocks --- */
#define STM32_PWR_BASE              0x40007000

#define STM32_PWR_CR                REG32(STM32_PWR_BASE + 0x00)
#define STM32_PWR_CSR               REG32(STM32_PWR_BASE + 0x04)

#if defined(CHIP_VARIANT_stm32l15x)
#define STM32_RCC_BASE              0x40023800

#define STM32_RCC_CR                REG32(STM32_RCC_BASE + 0x00)
#define STM32_RCC_ICSR              REG32(STM32_RCC_BASE + 0x04)
#define STM32_RCC_CFGR              REG32(STM32_RCC_BASE + 0x08)
#define STM32_RCC_CIR               REG32(STM32_RCC_BASE + 0x0C)
#define STM32_RCC_AHBRSTR           REG32(STM32_RCC_BASE + 0x10)
#define STM32_RCC_APB2RSTR          REG32(STM32_RCC_BASE + 0x14)
#define STM32_RCC_APB1RSTR          REG32(STM32_RCC_BASE + 0x18)
#define STM32_RCC_AHBENR            REG32(STM32_RCC_BASE + 0x1C)
#define STM32_RCC_APB2ENR           REG32(STM32_RCC_BASE + 0x20)
#define STM32_RCC_APB1ENR           REG32(STM32_RCC_BASE + 0x24)
#define STM32_RCC_AHBLPENR          REG32(STM32_RCC_BASE + 0x28)
#define STM32_RCC_APB2LPENR         REG32(STM32_RCC_BASE + 0x2C)
#define STM32_RCC_APB1LPENR         REG32(STM32_RCC_BASE + 0x30)
#define STM32_RCC_CSR               REG32(STM32_RCC_BASE + 0x34)

#define RCC_AHBENR_DMA1EN           (1 << 24)

#define STM32_SYSCFG_BASE           0x40010000

#define STM32_SYSCFG_MEMRMP         REG32(STM32_SYSCFG_BASE + 0x00)
#define STM32_SYSCFG_PMC            REG32(STM32_SYSCFG_BASE + 0x04)
#define STM32_SYSCFG_EXTICR(n)      REG32(STM32_SYSCFG_BASE + 8 + 4 * (n))

#elif defined(CHIP_VARIANT_stm32f100) || defined(CHIP_VARIANT_stm32f10x)
#define STM32_RCC_BASE              0x40021000

#define STM32_RCC_CR                REG32(STM32_RCC_BASE + 0x00)
#define STM32_RCC_CFGR              REG32(STM32_RCC_BASE + 0x04)
#define STM32_RCC_CIR               REG32(STM32_RCC_BASE + 0x08)
#define STM32_RCC_APB2RSTR          REG32(STM32_RCC_BASE + 0x0c)
#define STM32_RCC_APB1RSTR          REG32(STM32_RCC_BASE + 0x10)
#define STM32_RCC_AHBENR            REG32(STM32_RCC_BASE + 0x14)
#define STM32_RCC_APB2ENR           REG32(STM32_RCC_BASE + 0x18)
#define STM32_RCC_APB1ENR           REG32(STM32_RCC_BASE + 0x1c)
#define STM32_RCC_BDCR              REG32(STM32_RCC_BASE + 0x20)
#define STM32_RCC_CSR               REG32(STM32_RCC_BASE + 0x24)
#define STM32_RCC_CFGR2             REG32(STM32_RCC_BASE + 0x2c) /* STM32F100 */

#define RCC_AHBENR_DMA1EN           (1 << 0)

#else
#error Unsupported chip variant
#endif

/* --- Watchdogs --- */

#define STM32_WWDG_BASE             0x40002C00

#define STM32_WWDG_CR               REG32(STM32_WWDG_BASE + 0x00)
#define STM32_WWDG_CFR              REG32(STM32_WWDG_BASE + 0x04)
#define STM32_WWDG_SR               REG32(STM32_WWDG_BASE + 0x08)

#define STM32_WWDG_TB_8             (3 << 7)
#define STM32_WWDG_EWI              (1 << 9)

#define STM32_IWDG_BASE             0x40003000

#define STM32_IWDG_KR               REG32(STM32_IWDG_BASE + 0x00)
#define STM32_IWDG_PR               REG32(STM32_IWDG_BASE + 0x04)
#define STM32_IWDG_RLR              REG32(STM32_IWDG_BASE + 0x08)
#define STM32_IWDG_SR               REG32(STM32_IWDG_BASE + 0x0C)

/* --- Real-Time Clock --- */

#define STM32_RTC_BASE              0x40002800

#if defined(CHIP_VARIANT_stm32l15x)
#define STM32_RTC_TR                REG32(STM32_RTC_BASE + 0x00)
#define STM32_RTC_DR                REG32(STM32_RTC_BASE + 0x04)
#define STM32_RTC_CR                REG32(STM32_RTC_BASE + 0x08)
#define STM32_RTC_ISR               REG32(STM32_RTC_BASE + 0x0C)
#define STM32_RTC_PRER              REG32(STM32_RTC_BASE + 0x10)
#define STM32_RTC_WUTR              REG32(STM32_RTC_BASE + 0x14)
#define STM32_RTC_CALIBR            REG32(STM32_RTC_BASE + 0x18)
#define STM32_RTC_ALRMAR            REG32(STM32_RTC_BASE + 0x1C)
#define STM32_RTC_ALRMBR            REG32(STM32_RTC_BASE + 0x20)
#define STM32_RTC_WPR               REG32(STM32_RTC_BASE + 0x24)
#define STM32_RTC_TSTR              REG32(STM32_RTC_BASE + 0x30)
#define STM32_RTC_TSDR              REG32(STM32_RTC_BASE + 0x34)
#define STM32_RTC_TAFCR             REG32(STM32_RTC_BASE + 0x40)
#define STM32_RTC_BACKUP(n)         REG32(STM32_RTC_BASE + 0x50 + 4 * (n))

#define STM32_BKP_DATA(n)           STM32_RTC_BACKUP(n)
#define STM32_BKP_ENTRIES           20

#elif defined(CHIP_VARIANT_stm32f100) || defined(CHIP_VARIANT_stm32f10x)
#define STM32_RTC_CRH               REG32(STM32_RTC_BASE + 0x00)
#define STM32_RTC_CRL               REG32(STM32_RTC_BASE + 0x04)
#define STM32_RTC_PRLH              REG32(STM32_RTC_BASE + 0x08)
#define STM32_RTC_PRLL              REG16(STM32_RTC_BASE + 0x0c)
#define STM32_RTC_DIVH              REG16(STM32_RTC_BASE + 0x10)
#define STM32_RTC_DIVL              REG16(STM32_RTC_BASE + 0x14)
#define STM32_RTC_CNTH              REG16(STM32_RTC_BASE + 0x18)
#define STM32_RTC_CNTL              REG16(STM32_RTC_BASE + 0x1c)
#define STM32_RTC_ALRH              REG16(STM32_RTC_BASE + 0x20)
#define STM32_RTC_ALRL              REG16(STM32_RTC_BASE + 0x24)

/* --- Backup Registers --- */
#define STM32_BKP_BASE              0x40006c00

#if defined(CHIP_VARIANT_stm32f10x)
#define STM32_BKP_ENTRIES           42
#define STM32_BKP_DATA(n)	\
		REG16(STM32_BKP_BASE + (n < 11 ? 0x4 : 0x40) + 4 * (n))
#else
#define STM32_BKP_ENTRIES           10
#define STM32_BKP_DATA(n)           REG16(STM32_BKP_BASE + 0x4 + 4 * (n))
#endif

#define STM32_BKP_RTCCR             REG16(STM32_BKP_BASE + 0x2c)
#define STM32_BKP_CR                REG16(STM32_BKP_BASE + 0x30)
#define STM32_BKP_CSR               REG16(STM32_BKP_BASE + 0x34)

#define STM32_RTC_BACKUP(n)         STM32_BKP_DATA(n)

#else
#error Unsupported chip variant
#endif

/* --- SPI --- */
#define STM32_SPI1_BASE             0x40013000
#define STM32_SPI2_BASE             0x40003800
#define STM32_SPI3_BASE             0x40003c00 /* STM32F100 only */

#define STM32_SPI1_PORT             0
#define STM32_SPI2_PORT             1

/* The SPI controller registers */
struct stm32_spi_regs {
	uint16_t ctrl1;
	uint16_t _pad0;
	uint16_t ctrl2;
	uint16_t _pad1;
	unsigned stat;
	uint16_t data;
	uint16_t _pad2;
	unsigned crcp;
	unsigned rxcrc;
	unsigned txcrc;
	unsigned i2scfgr;	/* STM32F10x only */
	unsigned i2spr;		/* STM32F10x only */
};
/* Must be volatile, or compiler optimizes out repeated accesses */
typedef volatile struct stm32_spi_regs stm32_spi_regs_t;

#define STM32_SPI1_REGS ((stm32_spi_regs_t *)STM32_SPI1_BASE)

/* --- Debug --- */

#define STM32_DBGMCU_BASE           0xE0042000

#define STM32_DBGMCU_IDCODE         REG32(STM32_DBGMCU_BASE + 0x00)
#define STM32_DBGMCU_CR             REG32(STM32_DBGMCU_BASE + 0x04)
#define STM32_DBGMCU_APB1FZ         REG32(STM32_DBGMCU_BASE + 0x08)
#define STM32_DBGMCU_APB2FZ         REG32(STM32_DBGMCU_BASE + 0x0C)

/* --- Flash --- */

#if defined(CHIP_VARIANT_stm32l15x)
#define STM32_FLASH_REGS_BASE       0x40023c00

#define STM32_FLASH_ACR             REG32(STM32_FLASH_REGS_BASE + 0x00)
#define STM32_FLASH_PECR            REG32(STM32_FLASH_REGS_BASE + 0x04)
#define STM32_FLASH_PECR_PE_LOCK	(1 << 0)
#define STM32_FLASH_PECR_PRG_LOCK	(1 << 1)
#define STM32_FLASH_PECR_OPT_LOCK	(1 << 2)
#define STM32_FLASH_PECR_PROG		(1 << 3)
#define STM32_FLASH_PECR_ERASE		(1 << 9)
#define STM32_FLASH_PECR_FPRG		(1 << 10)
#define STM32_FLASH_PECR_OBL_LAUNCH	(1 << 18)
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

#define STM32_OPTB_BASE             0x1FF80000

#define STM32_OPTB_RDP_OFF          0x00
#define STM32_OPTB_USER_OFF         0x04
#define STM32_OPTB_WRP01            0x08
#define STM32_OPTB_WRP_OFF(n)       (0x08 + (n&1) + (n&2) * 2)
#define STM32_OPTB_WRP23            0x0c

#define STM32_OPTB_COMPL_SHIFT      16

#elif defined(CHIP_VARIANT_stm32f100) || defined(CHIP_VARIANT_stm32f10x)
#define STM32_FLASH_REGS_BASE       0x40022000

#define STM32_FLASH_ACR             REG32(STM32_FLASH_REGS_BASE + 0x00)
#define STM32_FLASH_KEYR            REG32(STM32_FLASH_REGS_BASE + 0x04)
#define STM32_FLASH_OPTKEYR         REG32(STM32_FLASH_REGS_BASE + 0x08)
#define STM32_FLASH_SR              REG32(STM32_FLASH_REGS_BASE + 0x0c)
#define STM32_FLASH_CR              REG32(STM32_FLASH_REGS_BASE + 0x10)
#define STM32_FLASH_AR              REG32(STM32_FLASH_REGS_BASE + 0x14)
#define STM32_FLASH_OBR             REG32(STM32_FLASH_REGS_BASE + 0x1c)
#define STM32_FLASH_WRPR            REG32(STM32_FLASH_REGS_BASE + 0x20)

#define STM32_OPTB_BASE             0x1FFFF800

#define STM32_OPTB_RDP_OFF          0x00
#define STM32_OPTB_USER_OFF         0x02
#define STM32_OPTB_WRP_OFF(n)       (0x08 + (n&3) * 2)
#define STM32_OPTB_WRP23            0x0c

#define STM32_OPTB_COMPL_SHIFT      8

#else
#error Unsupported chip variant
#endif

/* --- External Interrupts --- */
#define STM32_EXTI_BASE             0x40010400

#define STM32_EXTI_IMR              REG32(STM32_EXTI_BASE + 0x00)
#define STM32_EXTI_EMR              REG32(STM32_EXTI_BASE + 0x04)
#define STM32_EXTI_RTSR             REG32(STM32_EXTI_BASE + 0x08)
#define STM32_EXTI_FTSR             REG32(STM32_EXTI_BASE + 0x0c)
#define STM32_EXTI_SWIER            REG32(STM32_EXTI_BASE + 0x10)
#define STM32_EXTI_PR               REG32(STM32_EXTI_BASE + 0x14)

/* --- ADC --- */

#define STM32_ADC1_BASE             0x40012400
#define STM32_ADC_BASE              0x40012700 /* STM32L15X only */
#define STM32_ADC2_BASE             0x40012800 /* STM32F10x only */
#define STM32_ADC3_BASE             0x40013C00 /* STM32F10x only */

#if defined(CHIP_VARIANT_stm32f100)
#define STM32_ADC_SR               REG32(STM32_ADC1_BASE + 0x00)
#define STM32_ADC_CR1              REG32(STM32_ADC1_BASE + 0x04)
#define STM32_ADC_CR2              REG32(STM32_ADC1_BASE + 0x08)
#define STM32_ADC_SMPR1            REG32(STM32_ADC1_BASE + 0x0C)
#define STM32_ADC_SMPR2            REG32(STM32_ADC1_BASE + 0x10)
#define STM32_ADC_JOFR(n)          REG32(STM32_ADC1_BASE + 0x14 + ((n)&3) * 4)
#define STM32_ADC_HTR              REG32(STM32_ADC1_BASE + 0x24)
#define STM32_ADC_LTR              REG32(STM32_ADC1_BASE + 0x28)
#define STM32_ADC_SQR1             REG32(STM32_ADC1_BASE + 0x2C)
#define STM32_ADC_SQR2             REG32(STM32_ADC1_BASE + 0x30)
#define STM32_ADC_SQR3             REG32(STM32_ADC1_BASE + 0x34)
#define STM32_ADC_JSQR             REG32(STM32_ADC1_BASE + 0x38)
#define STM32_ADC_JDR(n)           REG32(STM32_ADC1_BASE + 0x3C + ((n)&3) * 4)
#define STM32_ADC_DR               REG32(STM32_ADC1_BASE + 0x4C)
#endif

/* --- MISC --- */

#define STM32_RI_BASE               0x40007C04
#define STM32_COMP_BASE             0x40007C00
#define STM32_CEC_BASE              0x40007800 /* STM32F100 only */
#define STM32_DAC_BASE              0x40007400
#define STM32_CRC_BASE              0x40023000
#define STM32_LCD_BASE              0x40002400
#if defined(CHIP_VARIANT_stm32l15x)
#define STM32_DMA1_BASE             0x40026000
#define STM32_DMA2_BASE             0x40026400
#elif defined(CHIP_VARIANT_stm32f100) || defined(CHIP_VARIANT_stm32f10x)
#define STM32_DMA1_BASE             0x40020000
/* FIXME: DMA2 is only available on high-density devices, but is used as part
 * of a sanity check in dma.c */
#define STM32_DMA2_BASE             0x40020400
#else
#error Unsupported chip variant
#endif

#define STM32_FSMC_BASE             0xA0000000 /* STM32F10x only */
#define STM32_USB_OTG_FS_BASE       0x50000000 /* STM32F10x only */
#define STM32_ETHERNET_BASE         0x40028000 /* STM32F10x only */
#define STM32_SDIO_BASE             0x40018000 /* STM32F10x only */
#define STM32_BXCAN1_BASE           0x40006400 /* STM32F10x only */
#define STM32_BXCAN2_BASE           0x40006800 /* STM32F10x only */
#define STM32_USB_CAN_SRAM_BASE     0x40006000 /* STM32F10x only */
#define STM32_USB_FS_BASE           0x40005C00 /* STM32F10x only */

#endif /* __CROS_EC_REGISTERS_H */
