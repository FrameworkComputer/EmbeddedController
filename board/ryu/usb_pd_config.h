/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

#include "adc.h"
#include "charge_state.h"
#include "clock.h"
#include "gpio.h"
#include "registers.h"

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 3
#define TIM_CLOCK_PD_RX_C0 2

#define TIM_CLOCK_PD_TX(p) TIM_CLOCK_PD_TX_C0
#define TIM_CLOCK_PD_RX(p) TIM_CLOCK_PD_RX_C0

/* Timer channel */
#define TIM_RX_CCR_C0 4
#define TIM_TX_CCR_C0 4

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

/* TX is using SPI1 on PA6, PB3, and PB5 */
#define SPI_REGS(p) STM32_SPI1_REGS

static inline void spi_enable_clock(int port)
{
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
}

#define DMAC_SPI_TX(p) STM32_DMAC_CH3

/* RX is using COMP1 triggering TIM2 CH4 */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM2_IC4
#define CMP2OUTSEL STM32_COMP_CMP2OUTSEL_TIM2_IC4

#define TIM_TX_CCR_IDX(p) TIM_TX_CCR_C0
#define TIM_RX_CCR_IDX(p) TIM_RX_CCR_C0
#define TIM_CCR_CS  1
#define EXTI_COMP_MASK(p) ((1 << 21) | (1 << 22))
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

#define DMAC_TIM_RX(p) STM32_DMAC_CH7

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/* 40 MHz pin speed on SPI MISO PA6 */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x00003000;
	/* 40 MHz pin speed on TIM3_CH4 (PB1) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x0000000C;
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
	/* put SPI function on TX pin : PA6 is SPI MISO */
	gpio_set_alternate_function(GPIO_A, 0x0040, 5);

	/* set the low level reference */
	gpio_set_level(GPIO_USBC_CC_TX_EN, 1);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	/* output low on SPI TX (PA6 is SPI1 MISO) to disable the FET */
	STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					& ~(3 << (2*6)))
					|  (1 << (2*6));

	/* put the low level reference in Hi-Z */
	gpio_set_level(GPIO_USBC_CC_TX_EN, 0);
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
	if (enable) {
		/* We never charging in power source mode */
		gpio_set_level(GPIO_USBC_CHARGE_EN_L, 1);
		/* High-Z is used for host mode. */
		gpio_set_level(GPIO_USBC_CC1_DEVICE_ODL, 1);
		gpio_set_level(GPIO_USBC_CC2_DEVICE_ODL, 1);
		/* Set 3.3V for Rp pull-up */
		gpio_set_flags(GPIO_USBC_CC_PUEN1, GPIO_OUT_HIGH);
		gpio_set_flags(GPIO_USBC_CC_PUEN2, GPIO_OUT_HIGH);
	} else {
		/* Kill VBUS power supply */
		charger_enable_otg_power(0);
		gpio_set_level(GPIO_CHGR_OTG, 0);
		/* Remove Rp pull-up by putting the high side in Hi-Z */
		gpio_set_flags(GPIO_USBC_CC_PUEN1, GPIO_INPUT);
		gpio_set_flags(GPIO_USBC_CC_PUEN2, GPIO_INPUT);
		/* Pull low for device mode. */
		gpio_set_level(GPIO_USBC_CC1_DEVICE_ODL, 0);
		gpio_set_level(GPIO_USBC_CC2_DEVICE_ODL, 0);
	}

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

	/* Reset mux ... for NONE polarity doesn't matter */
	board_set_usb_mux(port, TYPEC_MUX_NONE, USB_SWITCH_DISCONNECT, 0);

	gpio_set_level(GPIO_USBC_VCONN1_EN_L, 1);
	gpio_set_level(GPIO_USBC_VCONN2_EN_L, 1);
}

static inline int pd_adc_read(int port, int cc)
{
	if (cc == 0)
		return adc_read_channel(ADC_CC1_PD);
	else
		return adc_read_channel(ADC_CC2_PD);
}

static inline void pd_set_vconn(int port, int polarity, int enable)
{
	/* Set VCONN on the opposite CC line from the polarity */
	gpio_set_level(polarity ? GPIO_USBC_VCONN1_EN_L :
				  GPIO_USBC_VCONN2_EN_L, !enable);
}

#endif /* __USB_PD_CONFIG_H */
