/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for STM32L processor
 */

#ifndef __STM32L_REGISTERS
#define __STM32L_REGISTERS

#include <stdint.h>

/* concatenation helper */
#define STM32L_CAT(prefix, n, suffix) prefix ## n ## suffix

/* Macros to access registers */
#define REG32(addr) (*(volatile uint32_t*)(addr))
#define REG16(addr) (*(volatile uint16_t*)(addr))

/* IRQ numbers */
#define STM32L_IRQ_WWDG             0
#define STM32L_IRQ_PVD              1
#define STM32L_IRQ_TAMPER_STAMP     2
#define STM32L_IRQ_RTC_WAKEUP       3
#define STM32L_IRQ_FLASH            4
#define STM32L_IRQ_RCC              5
#define STM32L_IRQ_EXTI0            6
#define STM32L_IRQ_EXTI1            7
#define STM32L_IRQ_EXTI2            8
#define STM32L_IRQ_EXTI3            9
#define STM32L_IRQ_EXTI4           10
#define STM32L_IRQ_DMA_CHANNEL_1   11
#define STM32L_IRQ_DMA_CHANNEL_2   12
#define STM32L_IRQ_DMA_CHANNEL_3   13
#define STM32L_IRQ_DMA_CHANNEL_4   14
#define STM32L_IRQ_DMA_CHANNEL_5   15
#define STM32L_IRQ_DMA_CHANNEL_6   16
#define STM32L_IRQ_DMA_CHANNEL_7   17
#define STM32L_IRQ_ADC_1           18
#define STM32L_IRQ_USB_HP          19
#define STM32L_IRQ_USB_LP          20
#define STM32L_IRQ_DAC             21
#define STM32L_IRQ_COMP            22
#define STM32L_IRQ_EXTI9_5         23
#define STM32L_IRQ_LCD             24
#define STM32L_IRQ_TIM9            25
#define STM32L_IRQ_TIM10           26
#define STM32L_IRQ_TIM11           27
#define STM32L_IRQ_TIM2            28
#define STM32L_IRQ_TIM3            29
#define STM32L_IRQ_TIM4            30
#define STM32L_IRQ_I2C1_EV         31
#define STM32L_IRQ_I2C1_ER         32
#define STM32L_IRQ_I2C2_EV         33
#define STM32L_IRQ_I2C2_ER         34
#define STM32L_IRQ_SPI1            35
#define STM32L_IRQ_SPI2            36
#define STM32L_IRQ_USART1          37
#define STM32L_IRQ_USART2          38
#define STM32L_IRQ_USART3          39
#define STM32L_IRQ_EXTI15_10       40
#define STM32L_IRQ_RTC_ALARM       41
#define STM32L_IRQ_USB_FS_WAKEUP   42
#define STM32L_IRQ_TIM6            43
#define STM32L_IRQ_TIM7            44

/* --- USART --- */
#define STM32L_USART1_BASE          0x40013800
#define STM32L_USART2_BASE          0x40004400
#define STM32L_USART3_BASE          0x40004800

#define STM32L_USART_BASE(n)        STM32L_CAT(STM32L_USART, n, _BASE)

#define STM32L_USART_REG(n, offset) \
		REG16(STM32L_CAT(STM32L_USART, n, _BASE) + (offset))

#define STM32L_USART_SR(n)          STM32L_USART_REG(n, 0x00)
#define STM32L_USART_DR(n)          STM32L_USART_REG(n, 0x04)
#define STM32L_USART_BRR(n)         STM32L_USART_REG(n, 0x08)
#define STM32L_USART_CR1(n)         STM32L_USART_REG(n, 0x0C)
#define STM32L_USART_CR2(n)         STM32L_USART_REG(n, 0x10)
#define STM32L_USART_CR3(n)         STM32L_USART_REG(n, 0x14)
#define STM32L_USART_GTPR(n)        STM32L_USART_REG(n, 0x18)

#define STM32L_IRQ_USART(n)         STM32L_CAT(STM32L_IRQ_USART, n, )

/* --- TIMERS --- */
#define STM32L_TIM2_BASE            0x40000000
#define STM32L_TIM3_BASE            0x40000400
#define STM32L_TIM4_BASE            0x40000800
#define STM32L_TIM6_BASE            0x40001000
#define STM32L_TIM7_BASE            0x40001400
#define STM32L_TIM9_BASE            0x40010800
#define STM32L_TIM10_BASE           0x40010C00
#define STM32L_TIM11_BASE           0x40011000

#define STM32L_TIM_REG(n, offset) \
		REG16(STM32L_CAT(STM32L_TIM, n, _BASE) + (offset))

#define STM32L_TIM_CR1(n)           STM32L_TIM_REG(n, 0x00)
#define STM32L_TIM_CR2(n)           STM32L_TIM_REG(n, 0x04)
#define STM32L_TIM_SMCR(n)          STM32L_TIM_REG(n, 0x08)
#define STM32L_TIM_DIER(n)          STM32L_TIM_REG(n, 0x0C)
#define STM32L_TIM_SR(n)            STM32L_TIM_REG(n, 0x10)
#define STM32L_TIM_EGR(n)           STM32L_TIM_REG(n, 0x14)
#define STM32L_TIM_CCMR1(n)         STM32L_TIM_REG(n, 0x18)
#define STM32L_TIM_CCMR2(n)         STM32L_TIM_REG(n, 0x1C)
#define STM32L_TIM_CCER(n)          STM32L_TIM_REG(n, 0x20)
#define STM32L_TIM_CNT(n)           STM32L_TIM_REG(n, 0x24)
#define STM32L_TIM_PSC(n)           STM32L_TIM_REG(n, 0x28)
#define STM32L_TIM_ARR(n)           STM32L_TIM_REG(n, 0x2C)
#define STM32L_TIM_CCR1(n)          STM32L_TIM_REG(n, 0x34)
#define STM32L_TIM_CCR2(n)          STM32L_TIM_REG(n, 0x38)
#define STM32L_TIM_CCR3(n)          STM32L_TIM_REG(n, 0x3C)
#define STM32L_TIM_CCR4(n)          STM32L_TIM_REG(n, 0x40)
#define STM32L_TIM_DCR(n)           STM32L_TIM_REG(n, 0x48)
#define STM32L_TIM_DMAR(n)          STM32L_TIM_REG(n, 0x4C)
#define STM32L_TIM_OR(n)            STM32L_TIM_REG(n, 0x50)

/* --- GPIO --- */
#define STM32L_GPIOA_BASE            0x40020000
#define STM32L_GPIOB_BASE            0x40020400
#define STM32L_GPIOC_BASE            0x40020800
#define STM32L_GPIOD_BASE            0x40020C00
#define STM32L_GPIOE_BASE            0x40021000
#define STM32L_GPIOH_BASE            0x40021400

#define GPIO_A                       STM32L_GPIOA_BASE
#define GPIO_B                       STM32L_GPIOB_BASE
#define GPIO_C                       STM32L_GPIOC_BASE
#define GPIO_D                       STM32L_GPIOD_BASE
#define GPIO_E                       STM32L_GPIOE_BASE
#define GPIO_H                       STM32L_GPIOH_BASE

#define STM32L_GPIO_REG32(l, offset) \
		REG32(STM32L_CAT(STM32L_GPIO, l, _BASE) + (offset))
#define STM32L_GPIO_REG16(l, offset) \
		REG16(STM32L_CAT(STM32L_GPIO, l, _BASE) + (offset))

#define STM32L_GPIO_MODER(l)         STM32L_GPIO_REG32(l, 0x00)
#define STM32L_GPIO_OTYPER(l)        STM32L_GPIO_REG16(l, 0x04)
#define STM32L_GPIO_OSPEEDR(l)       STM32L_GPIO_REG32(l, 0x08)
#define STM32L_GPIO_PUPDR(l)         STM32L_GPIO_REG32(l, 0x0C)
#define STM32L_GPIO_IDR(l)           STM32L_GPIO_REG16(l, 0x10)
#define STM32L_GPIO_ODR(l)           STM32L_GPIO_REG16(l, 0x14)
#define STM32L_GPIO_BSRR(l)          STM32L_GPIO_REG32(l, 0x18)
#define STM32L_GPIO_LCKR(l)          STM32L_GPIO_REG32(l, 0x1C)
#define STM32L_GPIO_AFRL(l)          STM32L_GPIO_REG32(l, 0x20)
#define STM32L_GPIO_AFRH(l)          STM32L_GPIO_REG32(l, 0x24)

#define STM32L_GPIO_MODER_OFF(b)     REG32((b) + 0x00)
#define STM32L_GPIO_OTYPER_OFF(b)    REG16((b) + 0x04)
#define STM32L_GPIO_OSPEEDR_OFF(b)   REG32((b) + 0x08)
#define STM32L_GPIO_PUPDR_OFF(b)     REG32((b) + 0x0C)
#define STM32L_GPIO_IDR_OFF(b)       REG16((b) + 0x10)
#define STM32L_GPIO_ODR_OFF(b)       REG16((b) + 0x14)
#define STM32L_GPIO_BSRR_OFF(b)      REG32((b) + 0x18)
#define STM32L_GPIO_LCKR_OFF(b)      REG32((b) + 0x1C)
#define STM32L_GPIO_AFRL_OFF(b)      REG32((b) + 0x20)
#define STM32L_GPIO_AFRH_OFF(b)      REG32((b) + 0x24)

/* --- I2C --- */
#define STM32L_I2C1_BASE             0x40005400
#define STM32L_I2C2_BASE             0x40005800

#define STM32L_I2C_REG(n, offset) \
		REG16(STM32L_CAT(STM32L_I2C, n, _BASE) + (offset))

#define STM32L_I2C_CR1(n)            STM32L_I2C_REG(n, 0x00)
#define STM32L_I2C_CR2(n)            STM32L_I2C_REG(n, 0x04)
#define STM32L_I2C_OAR1(n)           STM32L_I2C_REG(n, 0x08)
#define STM32L_I2C_OAR2(n)           STM32L_I2C_REG(n, 0x0C)
#define STM32L_I2C_DR(n)             STM32L_I2C_REG(n, 0x10)
#define STM32L_I2C_SR1(n)            STM32L_I2C_REG(n, 0x14)
#define STM32L_I2C_SR2(n)            STM32L_I2C_REG(n, 0x18)
#define STM32L_I2C_CCR(n)            STM32L_I2C_REG(n, 0x1C)
#define STM32L_I2C_TRISE(n)          STM32L_I2C_REG(n, 0x20)

/* --- Power / Reset / Clocks --- */
#define STM32L_PWR_BASE              0x40007000

#define STM32L_PWR_CR                REG32(STM32L_PWR_BASE + 0x00)
#define STM32L_PWR_CSR               REG32(STM32L_PWR_BASE + 0x04)

#define STM32L_RCC_BASE              0x40023800

#define STM32L_RCC_CR                REG32(STM32L_RCC_BASE + 0x00)
#define STM32L_RCC_ICSR              REG32(STM32L_RCC_BASE + 0x04)
#define STM32L_RCC_CFGR              REG32(STM32L_RCC_BASE + 0x08)
#define STM32L_RCC_CIR               REG32(STM32L_RCC_BASE + 0x0C)
#define STM32L_RCC_AHBRSTR           REG32(STM32L_RCC_BASE + 0x10)
#define STM32L_RCC_APB2RSTR          REG32(STM32L_RCC_BASE + 0x14)
#define STM32L_RCC_APB1RSTR          REG32(STM32L_RCC_BASE + 0x18)
#define STM32L_RCC_AHBENR            REG32(STM32L_RCC_BASE + 0x1C)
#define STM32L_RCC_APB2ENR           REG32(STM32L_RCC_BASE + 0x20)
#define STM32L_RCC_APB1ENR           REG32(STM32L_RCC_BASE + 0x24)
#define STM32L_RCC_AHBLPENR          REG32(STM32L_RCC_BASE + 0x28)
#define STM32L_RCC_APB2LPENR         REG32(STM32L_RCC_BASE + 0x2C)
#define STM32L_RCC_APB1LPENR         REG32(STM32L_RCC_BASE + 0x30)
#define STM32L_RCC_CSR               REG32(STM32L_RCC_BASE + 0x34)

#define STM32L_SYSCFG_BASE           0x40010000

#define STM32L_SYSCFG_MEMRMP         REG32(STM32L_SYSCFG_BASE + 0x00)
#define STM32L_SYSCFG_PMC            REG32(STM32L_SYSCFG_BASE + 0x04)
#define STM32L_SYSCFG_EXTICR(n)      REG32(STM32L_SYSCFG_BASE + 8 + 4 * (n))

/* --- Watchdogs --- */

#define STM32L_WWDG_BASE             0x40002C00

#define STM32L_WWDG_CR               REG32(STM32L_WWDG_BASE + 0x00)
#define STM32L_WWDG_CFR              REG32(STM32L_WWDG_BASE + 0x04)
#define STM32L_WWDG_SR               REG32(STM32L_WWDG_BASE + 0x08)

#define STM32L_IWDG_BASE             0x40003000

#define STM32L_IWDG_KR               REG32(STM32L_IWDG_BASE + 0x00)
#define STM32L_IWDG_PR               REG32(STM32L_IWDG_BASE + 0x04)
#define STM32L_IWDG_RLR              REG32(STM32L_IWDG_BASE + 0x08)
#define STM32L_IWDG_SR               REG32(STM32L_IWDG_BASE + 0x0C)

/* --- Real-Time Clock --- */

#define STM32L_RTC_BASE              0x40002800

#define STM32L_RTC_TR                REG32(STM32L_RTC_BASE + 0x00)
#define STM32L_RTC_DR                REG32(STM32L_RTC_BASE + 0x04)
#define STM32L_RTC_CR                REG32(STM32L_RTC_BASE + 0x08)
#define STM32L_RTC_ISR               REG32(STM32L_RTC_BASE + 0x0C)
#define STM32L_RTC_PRER              REG32(STM32L_RTC_BASE + 0x10)
#define STM32L_RTC_WUTR              REG32(STM32L_RTC_BASE + 0x14)
#define STM32L_RTC_CALIBR            REG32(STM32L_RTC_BASE + 0x18)
#define STM32L_RTC_ALRMAR            REG32(STM32L_RTC_BASE + 0x1C)
#define STM32L_RTC_ALRMBR            REG32(STM32L_RTC_BASE + 0x20)
#define STM32L_RTC_WPR               REG32(STM32L_RTC_BASE + 0x24)
#define STM32L_RTC_TSTR              REG32(STM32L_RTC_BASE + 0x30)
#define STM32L_RTC_TSDR              REG32(STM32L_RTC_BASE + 0x34)
#define STM32L_RTC_TAFCR             REG32(STM32L_RTC_BASE + 0x40)
#define STM32L_RTC_BACKUP(n)         REG32(STM32L_RTC_BASE + 0x50 + 4 * (n))

/* --- Debug --- */

#define STM32L_DBGMCU_BASE           0xE0042000

#define STM32L_DBGMCU_IDCODE         REG32(STM32L_DBGMCU_BASE + 0x00)
#define STM32L_DBGMCU_CR             REG32(STM32L_DBGMCU_BASE + 0x04)
#define STM32L_DBGMCU_APB1FZ         REG32(STM32L_DBGMCU_BASE + 0x08)
#define STM32L_DBGMCU_APB2FZ         REG32(STM32L_DBGMCU_BASE + 0x0C)

/* --- Flash --- */

#define STM32L_FLASH_REGS_BASE       0x40023c00

#define STM32L_FLASH_ACR             REG32(STM32L_FLASH_REGS_BASE + 0x00)

/* --- MISC --- */

#define STM32L_RI_BASE               0x40007C04
#define STM32L_EXTI_BASE             0x40010400
#define STM32L_ADC1_BASE             0x40012400
#define STM32L_ADC_BASE              0x40012700
#define STM32L_COMP_BASE             0x40007C00
#define STM32L_DAC_BASE              0x40007400
#define STM32L_SPI1_BASE             0x40013000
#define STM32L_SPI2_BASE             0x40003800
#define STM32L_CRC_BASE              0x40023000
#define STM32L_LCD_BASE              0x40002400

#endif /* __STM32L_REGISTERS */
