/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "gpio.h"
#include "registers.h"

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 15
#define TIM_CLOCK_PD_RX_C0 1

#define TIM_CLOCK_PD_TX(p) TIM_CLOCK_PD_TX_C0
#define TIM_CLOCK_PD_RX(p) TIM_CLOCK_PD_RX_C0

/* Timer channel */
#define TIM_RX_CCR_C0 1
#define TIM_TX_CCR_C0 2

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

/* TX uses SPI1 on PB3-4 */
#define SPI_REGS(p) STM32_SPI1_REGS
static inline void spi_enable_clock(int port)
{
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
}

/* DMA for transmit uses DMA_CH3 */
#define DMAC_SPI_TX(p) STM32_DMAC_CH3

/* RX uses COMP1 and COMP2 on TIM1 CH1 */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM1_IC1
#define CMP2OUTSEL STM32_COMP_CMP2OUTSEL_TIM1_IC1

#define TIM_TX_CCR_IDX(p) TIM_TX_CCR_C0
#define TIM_RX_CCR_IDX(p) TIM_RX_CCR_C0
#define TIM_CCR_CS  1
#define EXTI_COMP_MASK(p) ((1 << 21) | (1 << 22))
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* DMA for receive uses DMA_CH2 */
#define DMAC_TIM_RX(p) STM32_DMAC_CH2

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/* 40 MHz pin speed on SPI PB3/4/15 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xC00003C0;
	/* 40 MHz pin speed on SPI PA6 */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x00003000;
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
	/* put SPI function on TX pin */
	if (polarity) {
		/* USB_C0_CC2_TX_DATA: PA6 is SPI1 MISO */
		gpio_set_alternate_function(GPIO_A, 0x0040, 0);
		/* MCU ADC PA3 pin output low */
		STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
				& ~(3 << (2*3))) /* PA3 disable ADC */
				|  (1 << (2*3)); /* Set as GPO */
		gpio_set_level(GPIO_USB_C0_CC2_PD, 0);
	} else {
		/* USB_C0_CC1_TX_DATA: PB4 is SPI1 MISO */
		gpio_set_alternate_function(GPIO_B, 0x0010, 0);
		/* MCU ADC PA1 pin output low */
		STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
				& ~(3 << (2*1))) /* PA1 disable ADC */
				|  (1 << (2*1)); /* Set as GPO */
		gpio_set_level(GPIO_USB_C0_CC1_PD, 0);
	}

}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	if (polarity) {
		/* Set TX_DATA to Hi-Z, PA6 is SPI1 MISO */
		STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
				& ~(3 << (2*6)));
		/* set ADC PA3 pin to ADC function (Hi-Z) */
		STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
				|  (3 << (2*3))); /* PA3 as ADC */
	} else {
		/* Set TX_DATA to Hi-Z, PB4 is SPI1 MISO */
		STM32_GPIO_MODER(GPIO_B) = (STM32_GPIO_MODER(GPIO_B)
				& ~(3 << (2*4)));
		/* set ADC PA1 pin to ADC function (Hi-Z) */
		STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
				|  (3 << (2*1))); /* PA1 as ADC */
	}
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
{
	/*
	 * use the right comparator : CC1 -> PA1 (COMP1 INP)
	 *                            CC2 -> PA3 (COMP2 INP)
	 * use VrefInt / 2 as INM (about 600mV)
	 */
	STM32_COMP_CSR = (STM32_COMP_CSR
		 & ~(STM32_COMP_CMP1INSEL_MASK | STM32_COMP_CMP2INSEL_MASK
		   | STM32_COMP_CMP1EN | STM32_COMP_CMP2EN))
		| STM32_COMP_CMP1INSEL_VREF12 | STM32_COMP_CMP2INSEL_VREF12
		| (polarity ? STM32_COMP_CMP2EN : STM32_COMP_CMP1EN);
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	gpio_config_module(MODULE_USB_PD, 1);
}

static inline void pd_set_host_mode(int port, int enable)
{
	/* We're always a pull-down, nothing to do here */
}

static inline void pd_config_init(int port, uint8_t power_role)
{
	/* Initialize TX pins and put them in Hi-Z */
	pd_tx_init();
}

static inline int pd_adc_read(int port, int cc)
{
	return adc_read_channel(cc ? ADC_C0_CC2_PD : ADC_C0_CC1_PD);
}

#endif /* __CROS_EC_USB_PD_CONFIG_H */
