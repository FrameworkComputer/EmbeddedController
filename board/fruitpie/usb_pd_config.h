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

/* TX is using SPI2 on PB12-14 */
#define SPI_REGS STM32_SPI2_REGS
#define DMAC_SPI_TX STM32_DMAC_CH7

static inline void spi_enable_clock(void)
{
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;
	STM32_SYSCFG_CFGR1 |= 1 << 24; /* Remap SPI2 DMA */
}

/* RX is using COMP1 triggering TIM1 CH1 */
#define DMAC_TIM_RX STM32_DMAC_CH2
#define TIM_CCR_IDX 1
#define TIM_CCR_CS  1
#define EXTI_COMP_MASK (1 << 21)
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(void)
{
	/* 40 MHz pin speed on SPI PB12/13/14 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x7f000000;
	/* 40 MHz pin speed on TIM17_CH1 (PB9) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000C0000;
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int polarity)
{
	gpio_set_level(GPIO_PD_TX_EN, 1);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int polarity)
{
	gpio_set_level(GPIO_PD_TX_EN, 0);
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int polarity)
{
	/* use the right comparator non inverted input for COMP1 */
	STM32_COMP_CSR = (STM32_COMP_CSR & ~STM32_COMP_CMP1INSEL_MASK)
		| STM32_COMP_CMP1EN
		| (polarity ? STM32_COMP_CMP1INSEL_INM4
			    : STM32_COMP_CMP1INSEL_INM6);
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	gpio_config_module(MODULE_USB_PD, 1);
}

static inline void pd_set_host_mode(int enable)
{
	gpio_set_level(GPIO_CC_HOST, enable);
}

/* Standard-current DFP : no-connect voltage is 1.55V */
#define PD_SRC_VNC 1550 /* mV */

/* UFP-side : threshold for DFP connection detection */
#define PD_SNK_VA   200 /* mV */

/* we are a dev board, wait for the user to tell us what we should do */
#define PD_DEFAULT_STATE PD_STATE_DISABLED

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TRANSITION_DELAY 50000 /* us */

#endif /* __USB_PD_CONFIG_H */
