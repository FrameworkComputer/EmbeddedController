/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

#include "board.h"

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 17
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

/* TX is using SPI1 on PA4-7 */
#define SPI_REGS(p) STM32_SPI1_REGS

static inline void spi_enable_clock(int port)
{
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
}

#define DMAC_SPI_TX(p) STM32_DMAC_CH3

/* RX is using COMP1 triggering TIM1 CH1 */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM1_IC1
#define CMP2OUTSEL 0

#define TIM_TX_CCR_IDX(p) TIM_TX_CCR_C0
#define TIM_RX_CCR_IDX(p) TIM_RX_CCR_C0
#define TIM_CCR_CS 1
#define EXTI_COMP_MASK(p) BIT(21)
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

#define DMAC_TIM_RX(p) STM32_DMAC_CH2

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/* 40 MHz pin speed on SPI1 (PA5/6) and CC1_TX_EN (PA3) */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x00003CC0;
	/* 40 MHz pin speed on TIM17_CH1 (PB9) and CC2_TX_EN (PB2) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000C0030;
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
	/* put SPI function on TX pin */
	/* PA6 is SPI1 MISO */
	gpio_set_alternate_function(GPIO_A, 0x0040, 0);

	/* set the polarity */
	gpio_set_level(GPIO_USBC_CC1_TX_EN, !polarity);
	gpio_set_level(GPIO_USBC_CC2_TX_EN, polarity);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	/* output low on SPI TX to disable the FET */
	/* PA6 is SPI1_MISO */
	STM32_GPIO_MODER(GPIO_A) =
		(STM32_GPIO_MODER(GPIO_A) & ~(3 << (2 * 6))) | (1 << (2 * 6));
	/* put the low level reference in Hi-Z */
	gpio_set_level(GPIO_USBC_CC1_TX_EN, 0);
	gpio_set_level(GPIO_USBC_CC2_TX_EN, 0);
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
{
	/* use the right comparator non inverted input for COMP1 */
	STM32_COMP_CSR = (STM32_COMP_CSR & ~STM32_COMP_CMP1INSEL_MASK) |
			 STM32_COMP_CMP1EN |
			 (polarity ? STM32_COMP_CMP1INSEL_INM4 :
				     STM32_COMP_CMP1INSEL_INM6);
	gpio_set_level(GPIO_USBC_POLARITY, polarity);
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	/* Configure SCK pin */
	gpio_config_module(MODULE_USB_PD, 1);
}

static inline void pd_set_host_mode(int port, int enable)
{
	board_pd_set_host_mode(enable);
}

/**
 * Initialize various GPIOs and interfaces to safe state at start of pd_task.
 *
 * These include:
 *   VBUS, charge path based on power role.
 *   Physical layer CC transmit.
 *   VCONNs disabled.
 *
 * @param port        USB-C port number
 * @param power_role  Power role of device
 */
static inline void pd_config_init(int port, uint8_t power_role)
{
	/*
	 * Set CC pull resistors, and charge_en and vbus_en GPIOs to match
	 * the initial role.
	 */
	pd_set_host_mode(port, power_role);

	/* Initialize TX pins and put them in Hi-Z */
	pd_tx_init();

	gpio_set_level(GPIO_USB_CC1_VCONN_EN_L, 1);
	gpio_set_level(GPIO_USB_CC2_VCONN_EN_L, 1);
}

static inline int pd_adc_read(int port, int cc)
{
	return board_fake_pd_adc_read(cc);
}

#endif /* __CROS_EC_USB_PD_CONFIG_H */
