/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

#include "ina2xx.h"

/* Port and task configuration */
#define PD_PORT_COUNT 1
#ifdef HAS_TASK_PD /* PD message injector mode */
#define PORT_TO_TASK_ID(port) TASK_ID_PD
#else
#define PORT_TO_TASK_ID(port) -1
#endif
#define TASK_ID_TO_PORT(id)   0

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 17
#define TIM_CLOCK_PD_RX_C0 1

#define TIM_CLOCK_PD_TX(p) TIM_CLOCK_PD_TX_C0
#define TIM_CLOCK_PD_RX(p) TIM_CLOCK_PD_RX_C0

/* TX and RX timer register */
#define TIM_REG_TX_C0 (STM32_TIM_BASE(TIM_CLOCK_PD_TX_C0))
#define TIM_REG_RX_C0 (STM32_TIM_BASE(TIM_CLOCK_PD_RX_C0))
#define TIM_REG_TX(p) TIM_REG_TX_C0
#define TIM_REG_RX(p) TIM_REG_RX_C0

/* Timer channel */
#define TIM_RX_CCR_C0 1
#define TIM_TX_CCR_C0 1

/* RX timer capture/compare register */
#define TIM_CCR_C0 (&STM32_TIM_CCRx(TIM_CLOCK_PD_RX_C0, TIM_RX_CCR_C0))
#define TIM_RX_CCR_REG(p) TIM_CCR_C0

/* use the hardware accelerator for CRC */
#define CONFIG_HW_CRC

/* TX is using SPI1 on PA6/PB4 */
#define SPI_REGS(p) STM32_SPI1_REGS
#define DMAC_SPI_TX(p) STM32_DMAC_CH3

static inline void spi_enable_clock(int port)
{
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
}

/* RX is using COMP1 or COMp2 triggering TIM1 CH1  */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM1_IC1
#define CMP2OUTSEL STM32_COMP_CMP2OUTSEL_TIM1_IC1

#define DMAC_TIM_RX(p) STM32_DMAC_CH2
#define TIM_RX_CCR_IDX(p) TIM_RX_CCR_C0
#define TIM_TX_CCR_IDX(p) TIM_TX_CCR_C0
#define TIM_CCR_CS  1
#define EXTI_COMP_MASK(p) ((1 << 21) | (1 << 22))
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/* 40 MHz pin speed on SPI TX PB4 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x00000300;
	/* 40 MHz pin speed on SPI TX PA6 */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x00003000;
	/* 40 MHz pin speed on TIM17_CH1 (PB9) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000C0000;
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
#if 0 /* Transmit only on the active CC line */
	if (polarity) {
		gpio_set_level(GPIO_CC2_TX_EN, 1);
		/* TX_DATA on PA6 is now connected to SPI1 */
		gpio_set_alternate_function(GPIO_A, 0x0040, 0);
	} else {
		gpio_set_level(GPIO_CC1_TX_EN, 1);
		/* TX_DATA on PB4 is now connected to SPI1 */
		gpio_set_alternate_function(GPIO_B, 0x0010, 0);
	}
#else /* Transmit on both CC lines */
	gpio_set_level(GPIO_CC2_TX_EN, 1);
	gpio_set_level(GPIO_CC1_TX_EN, 1);
	/* TX_DATA on PA6 is now connected to SPI1 */
	gpio_set_alternate_function(GPIO_A, 0x0040, 0);
	/* TX_DATA on PB4 is now connected to SPI1 */
	gpio_set_alternate_function(GPIO_B, 0x0010, 0);
#endif
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	/* TX_DATA on PB4 is an output low GPIO to disable the FET */
	STM32_GPIO_MODER(GPIO_B) = (STM32_GPIO_MODER(GPIO_B) & ~(3 << (2*4)))
							     |  (1 << (2*4));
	/* TX_DATA on PA6 is an output low GPIO to disable the FET */
	STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A) & ~(3 << (2*6)))
							     |  (1 << (2*6));
	/*
	 * Tri-state the low side after the high side
	 * to ensure we are not going above Vnc
	 */
	gpio_set_level(GPIO_CC1_TX_EN, 0);
	gpio_set_level(GPIO_CC2_TX_EN, 0);
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
{
	/* use the right comparator */
	STM32_COMP_CSR = (STM32_COMP_CSR
		& ~(STM32_COMP_CMP1INSEL_MASK | STM32_COMP_CMP2INSEL_MASK
		  |STM32_COMP_CMP1EN | STM32_COMP_CMP2EN))
		| STM32_COMP_CMP1INSEL_INM4 | STM32_COMP_CMP2INSEL_INM4
		| (polarity ? STM32_COMP_CMP2EN : STM32_COMP_CMP1EN);
}

/* Initialize pins used for clocking */
static inline void pd_tx_init(void)
{
	gpio_config_module(MODULE_USB_PD, 1);

	/* Detect when VBUS crosses the 4.5V threshold (1.25mV/bit) */
	ina2xx_write(0, INA2XX_REG_ALERT, 4500 * 100 / 125);
	ina2xx_write(0, INA2XX_REG_MASK, INA2XX_MASK_EN_BOL);
	/* start as a power consumer */
	gpio_set_level(GPIO_CC1_RD, 0);
	gpio_set_level(GPIO_CC2_RD, 0);
}

static inline void pd_set_host_mode(int port, int enable)
{
	if (enable) {
		gpio_set_level(GPIO_CC1_RD, 1);
		gpio_set_level(GPIO_CC2_RD, 1);
		/* set Rp by driving high RPUSB GPIO */
		gpio_set_flags(GPIO_CC1_RPUSB, GPIO_OUT_HIGH);
		gpio_set_flags(GPIO_CC2_RPUSB, GPIO_OUT_HIGH);
	} else {
		/* put back RPUSB GPIO in the default state and set Rd */
		gpio_set_flags(GPIO_CC1_RPUSB, GPIO_ODR_HIGH);
		gpio_set_flags(GPIO_CC2_RPUSB, GPIO_ODR_HIGH);
		gpio_set_level(GPIO_CC1_RD, 0);
		gpio_set_level(GPIO_CC2_RD, 0);
	}
}

static inline void pd_config_init(int port, uint8_t power_role)
{
	/*
	 * Set CC pull resistors, and charge_en and vbus_en GPIOs to match
	 * the initial role.
	 */
	pd_set_host_mode(port, power_role);

	/* Initialize TX pins and put them in Hi-Z */
	pd_tx_init();
}

static inline int pd_adc_read(int port, int cc)
{
	if (cc == 0)
		return adc_read_channel(ADC_CH_CC1_PD);
	else
		return adc_read_channel(ADC_CH_CC2_PD);
}

static inline int pd_snk_is_vbus_provided(int port)
{
	/* assume the alert was programmed to detect bus voltage above 4.5V */
	return (gpio_get_level(GPIO_VBUS_ALERT_L) == 0);
}

/* Standard-current DFP : no-connect voltage is 1.55V */
#define PD_SRC_VNC 1550 /* mV */

/* UFP-side : threshold for DFP connection detection */
#define PD_SNK_VA   200 /* mV */

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  50000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

#endif /* __USB_PD_CONFIG_H */
