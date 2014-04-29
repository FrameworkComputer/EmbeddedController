/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX 14
#define TIM_CLOCK_PD_RX  3

/* use the hardware accelerator for CRC */
#define CONFIG_HW_CRC

/* TX is using SPI1 on PA4-6 */
#define SPI_REGS STM32_SPI1_REGS
#define DMAC_SPI_TX STM32_DMAC_CH3

static inline void spi_enable_clock(void)
{
	/* Already done in hardware_init() */
}

/* RX is on TIM3 CH1 connected to TIM3 CH2 pin (PA7, not internal COMP) */
#define DMAC_TIM_RX STM32_DMAC_CH4
#define TIM_CCR_IDX 1
/* connect TIM3 CH1 to TIM3_CH2 input */
#define TIM_CCR_CS  2
#define EXTI_COMP_MASK (1 << 7)
#define IRQ_COMP STM32_IRQ_EXTI4_15
/* the RX is inverted, triggers on rising edge */
#define EXTI_XTSR STM32_EXTI_RTSR

/* Clock divider for RX edges timings (2.4Mhz counter from 48Mhz clock) */
#define RX_CLOCK_DIV (20 - 1)

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(void)
{
	/* Already done in hardware_init() */
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int polarity)
{
	/* Drive TX GND on PA4 */
	STM32_GPIO_BSRR(GPIO_A) = 1 << (4 + 16 /* Reset */);
	/* Drive SPI MISO on PA6 by putting it in AF mode  */
	STM32_GPIO_MODER(GPIO_A) |= 0x2 << (2*6);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int polarity)
{
	/* Put TX GND (PA4) in Hi-Z state */
	STM32_GPIO_BSRR(GPIO_A) = 1 << 4 /* Set */;
	/* Put SPI MISO (PA6) in Hi-Z by putting it in input mode  */
	STM32_GPIO_MODER(GPIO_A) &= ~(0x3 << (2*6));
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int polarity)
{
	/* captive cable : no polarity */
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	/* Already done in hardware_init() */
}

/* 3.0A DFP : no-connect voltage is 2.45V */
#define PD_SRC_VNC (2450 /*mV*/ * 4096 / 3300/* 12-bit ADC with 3.3V range */)

/* we are a power supply, boot as a power source waiting for a sink */
#define PD_DEFAULT_STATE PD_STATE_SRC_DISCONNECTED

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TRANSITION_DELAY 50000 /* us */

#endif /* __USB_PD_CONFIG_H */
