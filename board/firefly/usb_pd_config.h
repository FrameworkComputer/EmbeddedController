/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX 17
#define TIM_CLOCK_PD_RX 1

/* use the hardware accelerator for CRC */
#define CONFIG_HW_CRC

/* TX is using SPI2 on PB3/PB4 or PA6 */
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
	/* 40 MHz pin speed on SPI1 PA4/6/7 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x0000F300;
	/* 40 MHz pin speed on SPI1 PB3/4/5 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x00000FC0;
	/* 40 MHz pin speed on TIM17_CH1 (PB9) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000C0000;
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
	/* put SPI function on TX pin */
	if (polarity) /* PB4 is SPI1 MISO */
		gpio_set_alternate_function(GPIO_B, 0x0010, 0);
	else /* PA6 is SPI1 MISO */
		gpio_set_alternate_function(GPIO_A, 0x0040, 0);

	/* set the low level reference */
	gpio_set_level(polarity ? GPIO_PD_CC2_TX_EN : GPIO_PD_CC1_TX_EN, 0);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int polarity)
{
	/* put SPI TX in Hi-Z */
	if (polarity)
		gpio_set_alternate_function(GPIO_B, 0x0010, -1);
	else
		gpio_set_alternate_function(GPIO_A, 0x0040, -1);
	/* put the low level reference in Hi-Z */
	gpio_set_level(polarity ? GPIO_PD_CC2_TX_EN : GPIO_PD_CC1_TX_EN, 1);
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int polarity)
{
	/* use the right comparator */
	STM32_COMP_CSR =
		(STM32_COMP_CSR & ~(STM32_COMP_CMP1EN | STM32_COMP_CMP2EN))
		| (polarity ? STM32_COMP_CMP2EN : STM32_COMP_CMP1EN);
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	/* Configure SCK pin */
	gpio_config_module(MODULE_USB_PD, 1);
}

static inline void pd_set_host_mode(int enable)
{
}

static inline int pd_adc_read(int cc)
{
	if (cc == 0)
		return adc_read_channel(ADC_CH_CC1_PD);
	else
		return adc_read_channel(ADC_CH_CC2_PD);
}

static inline int pd_snk_is_vbus_provided(void)
{
	return 1;
}

/* Standard-current DFP : no-connect voltage is 1.55V */
#define PD_SRC_VNC 1550 /* mV */

/* UFP-side : threshold for DFP connection detection */
#define PD_SNK_VA   250 /* mV */

/* we are acting only as a sink */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* we are never a source : don't care about power supply */
#define PD_POWER_SUPPLY_TRANSITION_DELAY 0

#endif /* __USB_PD_CONFIG_H */
