/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

/* Port and task configuration */
#define PD_PORT_COUNT 1
#define PORT_TO_TASK_ID(port) TASK_ID_PD
#define TASK_ID_TO_PORT(id)   0

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 17
#define TIM_CLOCK_PD_RX_C0 1

#define TIM_CLOCK_PD_TX(p) TIM_CLOCK_PD_TX_C0
#define TIM_CLOCK_PD_RX(p) TIM_CLOCK_PD_RX_C0

/* Timer channel */
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

/* TX is using SPI2 on PB12-14 */
#define SPI_REGS(p) STM32_SPI2_REGS

static inline void spi_enable_clock(int port)
{
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;
	STM32_SYSCFG_CFGR1 |= 1 << 24; /* Remap SPI2 DMA */
}

#define DMAC_SPI_TX(p) STM32_DMAC_CH7

/* RX is using COMP1 triggering TIM1 CH1 */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM1_IC1
#define CMP2OUTSEL 0

#define TIM_CCR_IDX(p) TIM_RX_CCR_C0
#define TIM_CCR_CS  1
#define EXTI_COMP_MASK(p) (1 << 21)
#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

#define DMAC_TIM_RX(p) STM32_DMAC_CH2

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/* 40 MHz pin speed on SPI PB12/13/14 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x3f000000;
	/* 40 MHz pin speed on TIM17_CH1 (PB9) */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000C0000;
}

/* Reset SPI peripheral used for TX */
static inline void pd_tx_spi_reset(int port)
{
	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= (1 << 14);
	STM32_RCC_APB1RSTR &= ~(1 << 14);
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int port, int polarity)
{
	/* TX_DATA on PB14 is now connected to SPI2 */
	gpio_set_alternate_function(GPIO_B, 0x4000, 0);

	gpio_set_level(GPIO_PD_TX_EN, 1);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	/* TX_DATA on PB14 is an output low GPIO to disable the FET */
	STM32_GPIO_MODER(GPIO_B) = (STM32_GPIO_MODER(GPIO_B) & ~(3 << (2*14)))
							     |  (1 << (2*14));
	/*
	 * Tri-state the low side after the high side
	 * to ensure we are not going above Vnc
	 */
	gpio_set_level(GPIO_PD_TX_EN, 0);
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
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

static inline void pd_set_host_mode(int port, int enable)
{
	/* We never charging in power source mode */
	if (enable) {
		gpio_set_level(GPIO_CHARGE_EN_L, 1);
	} else {
		/* Kill VBUS power supply */
		gpio_set_level(GPIO_USB_C_5V_EN, 0);
		/* Enable the charging path*/
		gpio_set_level(GPIO_CHARGE_EN_L, 0);
	}

	gpio_set_level(GPIO_CC_HOST, enable);
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
	return gpio_get_level(GPIO_VBUS_WAKE);
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
