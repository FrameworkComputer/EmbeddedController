/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX 14
#define TIM_CLOCK_PD_RX 1

/* use the hardware accelerator for CRC */
#define CONFIG_HW_CRC

/* TX is using SPI1 on PB3-5 */
#define SPI_REGS STM32_SPI1_REGS
#define DMAC_SPI_TX STM32_DMAC_CH3

static inline void spi_enable_clock(void)
{
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
}

/* RX is using COMP1 triggering TIM1 CH1 */
#define DMAC_TIM_RX STM32_DMAC_CH2
#define TIM_CCR_IDX 1
#define TIM_CCR_CS  1
#define EXTI_COMP_MASK ((1 << 21) | (1 << 22))
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(void)
{
	/* 40 MHz pin speed on SPI MISO PA6 */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x00003000;
	/* 40 MHz pin speed on TIM14_CH1 (PB1) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x0000000C;
}

/* Reset SPI peripheral used for TX */
static inline void pd_tx_spi_reset(void)
{
	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= (1 << 12);
	STM32_RCC_APB2RSTR &= ~(1 << 12);
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int polarity)
{
	/* put SPI function on TX pin : PA6 is SPI MISO */
	gpio_set_alternate_function(GPIO_A, 0x0040, 0);

	/* set the low level reference */
	gpio_set_level(GPIO_USBC_CC_TX_EN, 1);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int polarity)
{
	/* output low on SPI TX (PA6 is SPI1 MISO) to disable the FET */
	STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					& ~(3 << (2*6)))
					|  (1 << (2*6));

	/* put the low level reference in Hi-Z */
	gpio_set_level(GPIO_USBC_CC_TX_EN, 0);
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int polarity)
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

static inline void pd_set_host_mode(int enable)
{
	if (enable) {
		/* We never charging in power source mode */
		gpio_set_level(GPIO_USBC_CHARGE_EN_L, 1);
		/* High-Z is used for host mode. */
		gpio_set_level(GPIO_USBC_CC1_DEVICE_ODL, 1);
		gpio_set_level(GPIO_USBC_CC2_DEVICE_ODL, 1);
	} else {
		/* Kill VBUS power supply */
		gpio_set_level(GPIO_USBC_5V_EN, 0);
		/* Pull low for device mode. */
		gpio_set_level(GPIO_USBC_CC1_DEVICE_ODL, 0);
		gpio_set_level(GPIO_USBC_CC2_DEVICE_ODL, 0);
		/* Enable the charging path*/
		gpio_set_level(GPIO_USBC_CHARGE_EN_L, 0);
	}

}

static inline int pd_adc_read(int cc)
{
	if (cc == 0)
		return adc_read_channel(ADC_CC1_PD);
	else
		return adc_read_channel(ADC_CC2_PD);
}

static inline int pd_snk_is_vbus_provided(void)
{
	return gpio_get_level(GPIO_CHGR_ACOK);
}

/* Standard-current DFP : no-connect voltage is 1.55V */
#define PD_SRC_VNC 1550 /* mV */

/* UFP-side : threshold for DFP connection detection */
#define PD_SNK_VA   200 /* mV */

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TRANSITION_DELAY 50000 /* us */

#endif /* __USB_PD_CONFIG_H */
