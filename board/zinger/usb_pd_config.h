/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 14
#define TIM_CLOCK_PD_RX_C0 3

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

/* TX is using SPI1 on PA4-6 */
#define SPI_REGS(p) STM32_SPI1_REGS

static inline void spi_enable_clock(int port)
{
	/* Already done in hardware_init() */
}

#define DMAC_SPI_TX(p) STM32_DMAC_CH3

/* RX is on TIM3 CH1 connected to TIM3 CH2 pin (PA7, not internal COMP) */
#define TIM_TX_CCR_IDX(p) TIM_TX_CCR_C0
#define TIM_RX_CCR_IDX(p) TIM_RX_CCR_C0
/* connect TIM3 CH1 to TIM3_CH2 input */
#define TIM_CCR_CS 2
#define EXTI_COMP_MASK(p) BIT(7)
#define IRQ_COMP STM32_IRQ_EXTI4_15
/* the RX is inverted, triggers on rising edge */
#define EXTI_XTSR STM32_EXTI_RTSR

#define DMAC_TIM_RX(p) STM32_DMAC_CH4

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	/* Already done in hardware_init() */
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
	/* Drive SPI MISO on PA6 by putting it in AF mode  */
	STM32_GPIO_MODER(GPIO_A) |= 0x2 << (2 * 6);
	/* Drive TX GND on PA4 */
	STM32_GPIO_BSRR(GPIO_A) = 1 << (4 + 16 /* Reset */);
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	/* Put TX GND (PA4) in Hi-Z state */
	STM32_GPIO_BSRR(GPIO_A) = BIT(4) /* Set */;
	/* Put SPI MISO (PA6) in Hi-Z by putting it in input mode  */
	STM32_GPIO_MODER(GPIO_A) &= ~(0x3 << (2 * 6));
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
{
	/* captive cable : no polarity */
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	/* Already done in hardware_init() */
}

static inline void pd_config_init(int port, uint8_t power_role)
{
}

static inline int pd_adc_read(int port, int cc)
{
	/* only one CC line, assume other one is always high */
	return (cc == 0) ? adc_read_channel(ADC_CH_CC1_PD) : 4096;
}

#endif /* __CROS_EC_USB_PD_CONFIG_H */
