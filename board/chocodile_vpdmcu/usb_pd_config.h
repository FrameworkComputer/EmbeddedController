/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "chip/stm32/registers.h"
#include "ec_commands.h"
#include "gpio.h"
#include "vpd_api.h"

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 16
#define TIM_CLOCK_PD_RX_C0 1

#define TIM_CLOCK_PD_TX(p) TIM_CLOCK_PD_TX_C0
#define TIM_CLOCK_PD_RX(p) TIM_CLOCK_PD_RX_C0

/* Timer channel */
#define TIM_TX_CCR_C0 1
#define TIM_RX_CCR_C0 1

/* RX timer capture/compare register */
#define TIM_CCR_C0 (&STM32_TIM_CCRx(TIM_CLOCK_PD_RX_C0, TIM_RX_CCR_C0))
#define TIM_RX_CCR_REG(p) TIM_CCR_C0

/* TX and RX timer register */
#define TIM_REG_TX_C0 (STM32_TIM_BASE(TIM_CLOCK_PD_TX_C0))
#define TIM_REG_RX_C0 (STM32_TIM_BASE(TIM_CLOCK_PD_RX_C0))
#define TIM_REG_TX(p) TIM_REG_TX_C0
#define TIM_REG_RX(p) TIM_REG_RX_C0

/* use the hardware accelerator for CRC */
#define CONFIG_HW_CRC

/* TX uses SPI1 on PB3-4 for port C0 */
#define SPI_REGS(p) STM32_SPI1_REGS

static inline void spi_enable_clock(int port)
{
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
}

/* SPI1_TX no remap needed */
#define DMAC_SPI_TX(p) STM32_DMAC_CH3

/* RX is using COMP1 triggering TIM1 CH1 */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM1_IC1
#define CMP2OUTSEL 0

#define TIM_TX_CCR_IDX(p) TIM_TX_CCR_C0
#define TIM_RX_CCR_IDX(p) TIM_RX_CCR_C0
#define TIM_CCR_CS  1

/* EXTI line 21 is connected to the CMP1 output */
#define EXTI_COMP1_MASK (1 << 21)
/* EXTI line 22 is connected to the CMP1 output */
#define EXTI_COMP2_MASK (1 << 22)

#define EXTI_COMP_MASK(p) (EXTI_COMP1_MASK | EXTI_COMP2_MASK)
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* TIM1_CH1 no remap needed */
#define DMAC_TIM_RX(p) STM32_DMAC_CH2

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/*
	 * 40 MHz pin speed on SPI PB3&4,
	 * (USB_C0_TX_CLKIN & USB_C0_CC1_TX_DATA)
	 *
	 * 40 MHz pin speed on TIM17_CH1 (PB7),
	 * (PD_TX_CLK_OUT)
	 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x0000C3C0;
}

/* Reset SPI peripheral used for TX */
static inline void pd_tx_spi_reset(int port)
{
	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= (1 << 12);
	STM32_RCC_APB2RSTR &= ~(1 << 12);
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int port, int polarity)
{
	/* USB_CC_TX_DATA: PB4 is SPI1 MISO */
	STM32_GPIO_MODER(GPIO_B) = (STM32_GPIO_MODER(GPIO_B)
			& ~(3 << (2*4))) /* PB4 disable ADC */
			|  (2 << (2*4)); /* Set as SPI1_MISO */
	/* MCU ADC PA1 pin output low */
	STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
			& ~(3 << (2*1))) /* PA1 disable ADC */
			|  (1 << (2*1)); /* Set as GPO */
	gpio_set_level(GPIO_CC_VPDMCU, 0);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	/* Set CC_TX_DATA to Hi-Z, PB4 is SPI1 MISO */
	STM32_GPIO_MODER(GPIO_B) = (STM32_GPIO_MODER(GPIO_B)
			& ~(3 << (2*4)));
	/* set ADC PA1 pin to ADC function (Hi-Z) */
	STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
			|  (3 << (2*1))); /* PA1 as ADC */
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
{
	/*
	 * use the right comparator : CC1 -> PA1 (COMP1 INP)
	 * use VrefInt / 2 as INM (about 600mV)
	 */
	STM32_COMP_CSR = (STM32_COMP_CSR & ~STM32_COMP_CMP1INSEL_MASK)
		| STM32_COMP_CMP1EN | STM32_COMP_CMP1INSEL_VREF12;
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	gpio_config_module(MODULE_USB_PD, 1);
}

static inline void pd_set_host_mode(int port, int enable)
{
	/* Do nothing */
}

/**
 * Initialize various GPIOs and interfaces to safe state at start of pd_task.
 *
 * These include:
 *   Physical layer CC transmit.
 *
 * @param port        USB-C port number
 * @param power_role  Power role of device
 */
static inline void pd_config_init(int port, uint8_t power_role)
{
	/* Initialize TX pins and put them in Hi-Z */
	pd_tx_init();
	pd_tx_disable(0, 0);
}

static inline int pd_adc_read(int port, int cc)
{
	return 0;
}

#endif /* __CROS_EC_USB_PD_CONFIG_H */

