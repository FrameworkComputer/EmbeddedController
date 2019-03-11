/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 16
#define TIM_CLOCK_PD_RX_C0 1

#define TIM_CLOCK_PD_TX(p) TIM_CLOCK_PD_TX_C0
#define TIM_CLOCK_PD_RX(p) TIM_CLOCK_PD_RX_C0

/* Timer channel */
#define TIM_RX_CCR_C0 1
#define TIM_TX_CCR_C0 1

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

/* TX is using SPI1 on PB3-4 */
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
#define EXTI_COMP_MASK(p) BIT(21)
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* TIM1_CH1 no remap needed */
#define DMAC_TIM_RX(p) STM32_DMAC_CH2

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/* 40 Mhz pin speed on TX_EN (PA15) */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0xC0000000;
	/* 40 MHz pin speed on SPI CLK/MOSI (PB3/4) TIM17_CH1 (PB9) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000C03C0;
}

/* Reset SPI peripheral used for TX */
static inline void pd_tx_spi_reset(int port)
{
	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= BIT(12);
	STM32_RCC_APB2RSTR &= ~BIT(12);
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int port, int polarity)
{
	/* PB4 is SPI1_MISO */
	gpio_set_alternate_function(GPIO_B, 0x0010, 0);
	/* USB_C_CC1_PD: PA1 output low */
	gpio_set_flags(GPIO_USB_C_CC1_PD, GPIO_OUTPUT);
	gpio_set_level(GPIO_USB_C_CC1_PD, 0);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	/* SPI TX (PB4) Hi-Z */
	gpio_set_flags(GPIO_PD_CC1_TX_DATA, GPIO_INPUT);
	/* put the low level reference in Hi-Z */
	gpio_set_flags(GPIO_USB_C_CC1_PD, GPIO_ANALOG);
}

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
	if (enable) {
		gpio_set_level(GPIO_PD_CC1_ODL, 1);
		gpio_set_flags(GPIO_PD_CC1_HOST_HIGH, GPIO_OUTPUT);
		gpio_set_level(GPIO_PD_CC1_HOST_HIGH, 1);
	} else {
		gpio_set_flags(GPIO_PD_CC1_HOST_HIGH, GPIO_INPUT);
		gpio_set_level(GPIO_PD_CC1_ODL, 0);
	}
}

static inline void pd_config_init(int port, uint8_t power_role)
{
	/* Initialize TX pins and put them in Hi-Z */
	pd_tx_init();
}

static inline int pd_adc_read(int port, int cc)
{
	if (cc == 0)
		return adc_read_channel(ADC_CH_CC1_PD);
	/*
	 * Check HOST_HIGH Rp setting.
	 * Return 3300mV on host mode.
	 */
	if ((STM32_GPIO_MODER(GPIO_B) & (3 << (2*5))) == (1 << (2*5)))
		return 3300;
	else
		return 0;
}

#endif /* __CROS_EC_USB_PD_CONFIG_H */
