/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "chip/stm32/registers.h"
#include "gpio.h"
#include "ec_commands.h"

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_C0 16
#define TIM_CLOCK_PD_RX_C0 1
#define TIM_CLOCK_PD_TX_C1 15
#define TIM_CLOCK_PD_RX_C1 3

/* Timer channel */
#define TIM_TX_CCR_C0 1
#define TIM_RX_CCR_C0 1
#define TIM_TX_CCR_C1 2
#define TIM_RX_CCR_C1 1

#define TIM_CLOCK_PD_TX(p) ((p) ? TIM_CLOCK_PD_TX_C1 : TIM_CLOCK_PD_TX_C0)
#define TIM_CLOCK_PD_RX(p) ((p) ? TIM_CLOCK_PD_RX_C1 : TIM_CLOCK_PD_RX_C0)

/* RX timer capture/compare register */
#define TIM_CCR_C0 (&STM32_TIM_CCRx(TIM_CLOCK_PD_RX_C0, TIM_RX_CCR_C0))
#define TIM_CCR_C1 (&STM32_TIM_CCRx(TIM_CLOCK_PD_RX_C1, TIM_RX_CCR_C1))
#define TIM_RX_CCR_REG(p) ((p) ? TIM_CCR_C1 : TIM_CCR_C0)

/* TX and RX timer register */
#define TIM_REG_TX_C0 (STM32_TIM_BASE(TIM_CLOCK_PD_TX_C0))
#define TIM_REG_RX_C0 (STM32_TIM_BASE(TIM_CLOCK_PD_RX_C0))
#define TIM_REG_TX_C1 (STM32_TIM_BASE(TIM_CLOCK_PD_TX_C1))
#define TIM_REG_RX_C1 (STM32_TIM_BASE(TIM_CLOCK_PD_RX_C1))
#define TIM_REG_TX(p) ((p) ? TIM_REG_TX_C1 : TIM_REG_TX_C0)
#define TIM_REG_RX(p) ((p) ? TIM_REG_RX_C1 : TIM_REG_RX_C0)

/* use the hardware accelerator for CRC */
#define CONFIG_HW_CRC

/* TX uses SPI1 on PB3-4 for port C0, SPI2 on PB 13-14 for port C1 */
#define SPI_REGS(p) ((p) ? STM32_SPI2_REGS : STM32_SPI1_REGS)
static inline void spi_enable_clock(int port)
{
	if (port == 0)
		STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
	else
		STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;
}

/* DMA for transmit uses DMA CH3 for C0 and DMA_CH5 for C1 */
#define DMAC_SPI_TX(p) ((p) ? STM32_DMAC_CH5 : STM32_DMAC_CH3)

/* RX uses COMP1 and TIM1 CH1 on port C0 and COMP2 and TIM3_CH1 for port C1*/
/* C1 RX use CMP1, TIM3_CH1, DMA_CH4 */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM3_IC1
/* C0 RX use CMP2, TIM1_CH1, DMA_CH2 */
#define CMP2OUTSEL STM32_COMP_CMP2OUTSEL_TIM1_IC1

#define TIM_TX_CCR_IDX(p) ((p) ? TIM_TX_CCR_C1 : TIM_TX_CCR_C0)
#define TIM_RX_CCR_IDX(p) ((p) ? TIM_RX_CCR_C1 : TIM_RX_CCR_C0)
#define TIM_CCR_CS  1

/*
 * EXTI line 21 is connected to the CMP1 output,
 * EXTI line 22 is connected to the CMP2 output,
 * C0 uses CMP2, and C1 uses CMP1.
 */
#define EXTI_COMP_MASK(p) ((p) ? (1<<21) : (1 << 22))

#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* DMA for receive uses DMA_CH2 for C0 and DMA_CH4 for C1 */
#define DMAC_TIM_RX(p) ((p) ? STM32_DMAC_CH4 : STM32_DMAC_CH2)

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	if (port == 0) {
		/* 40 MHz pin speed on SPI PB3&4,
		 * (USB_C0_TX_CLKIN & USB_C0_CC1_TX_DATA)
		 */
		STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000003C0;
		/* 40 MHz pin speed on TIM16_CH1 (PB8),
		 * (USB_C0_TX_CLKOUT)
		 */
		STM32_GPIO_OSPEEDR(GPIO_B) |= 0x00030000;
	} else {
		/* 40 MHz pin speed on SPI PB13/14,
		 * (USB_C1_TX_CLKIN & USB_C1_CCX_TX_DATA)
		 */
		STM32_GPIO_OSPEEDR(GPIO_B) |= 0x3C000000;
		/* 40 MHz pin speed on TIM15_CH2 (PB15) */
		STM32_GPIO_OSPEEDR(GPIO_B) |= 0xC0000000;
	}
}

/* Reset SPI peripheral used for TX */
static inline void pd_tx_spi_reset(int port)
{
	if (port == 0) {
		/* Reset SPI1 */
		STM32_RCC_APB2RSTR |= (1 << 12);
		STM32_RCC_APB2RSTR &= ~(1 << 12);
	} else {
		/* Reset SPI2 */
		STM32_RCC_APB1RSTR |= (1 << 14);
		STM32_RCC_APB1RSTR &= ~(1 << 14);
	}
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int port, int polarity)
{
	if (port == 0) {
		/* put SPI function on TX pin */
		if (polarity) {
			/* USB_C0_CC2_TX_DATA: PA6 is SPI1 MISO */
			gpio_set_alternate_function(GPIO_A, 0x0040, 0);
			/* MCU ADC PA4 pin output low */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					& ~(3 << (2*4))) /* PA4 disable ADC */
					|  (1 << (2*4)); /* Set as GPO */
			gpio_set_level(GPIO_USB_C0_CC2_PD, 0);
		} else {
			/* USB_C0_CC1_TX_DATA: PB4 is SPI1 MISO */
			gpio_set_alternate_function(GPIO_B, 0x0010, 0);
			/* MCU ADC PA2 pin output low */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					& ~(3 << (2*2))) /* PA2 disable ADC */
					|  (1 << (2*2)); /* Set as GPO */
			gpio_set_level(GPIO_USB_C0_CC1_PD, 0);
		}
	} else {
		/* put SPI function on TX pin */
		/* USB_C1_CCX_TX_DATA: PB14 is SPI1 MISO */
		gpio_set_alternate_function(GPIO_B, 0x4000, 0);
		/* MCU ADC pin output low */
		if (polarity) {
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					& ~(3 << (2*5))) /* PA5 disable ADC */
					|  (1 << (2*5)); /* Set as GPO */
			gpio_set_level(GPIO_USB_C1_CC2_PD, 0);
		} else {
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					& ~(3 << (2*0))) /* PA0 disable ADC */
					|  (1 << (2*0)); /* Set as GPO */
			gpio_set_level(GPIO_USB_C1_CC2_PD, 0);
		}

		/*
		 * There is a pin muxer to select CC1 or CC2 TX_DATA,
		 * Pin mux is controlled by USB_C1_CC2_TX_SEL pin,
		 * USB_C1_CC1_TX_DATA will be selected, if polarity is 0,
		 * USB_C1_CC2_TX_DATA will be selected, if polarity is 1 .
		 */
		gpio_set_level(GPIO_USB_C1_CC2_TX_SEL, polarity);
	}
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	if (port == 0) {
		/* output low on SPI TX to disable the FET */
		if (polarity) {/* PA6 is SPI1 MISO */
			gpio_set_alternate_function(GPIO_A, 0x0040, -1);
			/* set ADC PA4 pin to ADC function (Hi-Z) */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					|  (3 << (2*4))) /* PA4 as ADC */
					& ~(1 << (2*4)); /* disable GPO */
		} else {/* PB4 is SPI1 MISO */
			gpio_set_alternate_function(GPIO_B, 0x0010, -1);
			/* set ADC PA4 pin to ADC function (Hi-Z) */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					|  (3 << (2*2))) /* PA2 as ADC */
					& ~(1 << (2*2)); /* disable GPO */
		}
	} else {
		/* output low on SPI TX to disable the FET (PB14) */
		gpio_set_alternate_function(GPIO_B, 0x4000, -1);
		if (polarity) {
			/* set ADC PA4 pin to ADC function (Hi-Z) */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					|  (3 << (2*5))) /* PA5 as ADC */
					& ~(1 << (2*5)); /* disable GPO */
		} else {
			/* set ADC PA4 pin to ADC function (Hi-Z) */
			STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A)
					|  (3 << (2*0))) /* PA0 as ADC */
					& ~(1 << (2*0)); /* disable GPO */
		}
	}
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
{
	uint32_t val = STM32_COMP_CSR;

	/* Use window mode so that COMP1 and COMP2 share non-inverting input */
	val |= STM32_COMP_CMP1EN | STM32_COMP_CMP2EN | STM32_COMP_WNDWEN;

	if (port == 0) {
		/* C0 use the right comparator inverted input for COMP2 */
		STM32_COMP_CSR = (val & ~STM32_COMP_CMP2INSEL_MASK) |
			(polarity ? STM32_COMP_CMP2INSEL_INM4  /* PA4: C0_CC2 */
				  : STM32_COMP_CMP2INSEL_INM6);/* PA2: C0_CC1 */
	} else {
		/* C1 use the right comparator inverted input for COMP1 */
		STM32_COMP_CSR = (val & ~STM32_COMP_CMP1INSEL_MASK) |
			(polarity ? STM32_COMP_CMP1INSEL_INM5  /* PA5: C1_CC2 */
				  : STM32_COMP_CMP1INSEL_INM6);/* PA0: C1_CC1 */
	}
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	gpio_config_module(MODULE_USB_PD, 1);
}
static inline void pd_set_host_mode(int port, int enable)
{
	if (port == 0) {
		if (enable) {
			/* Pull up for host mode */
			gpio_set_flags(GPIO_USB_C0_HOST_HIGH, GPIO_OUTPUT);
			gpio_set_level(GPIO_USB_C0_HOST_HIGH, 1);
			/* High-Z is used for host mode. */
			gpio_set_level(GPIO_USB_C0_CC1_ODL, 1);
			gpio_set_level(GPIO_USB_C0_CC2_ODL, 1);
			/* Set TX Hi-Z */
			gpio_set_flags(GPIO_USB_C0_CC1_TX_DATA, GPIO_INPUT);
			gpio_set_flags(GPIO_USB_C0_CC2_TX_DATA, GPIO_INPUT);
		} else {
			/* Set HOST_HIGH to High-Z for device mode. */
			gpio_set_flags(GPIO_USB_C0_HOST_HIGH, GPIO_INPUT);
			/* Pull low for device mode. */
			gpio_set_level(GPIO_USB_C0_CC1_ODL, 0);
			gpio_set_level(GPIO_USB_C0_CC2_ODL, 0);
		}
	} else {
		if (enable) {
			/* Pull up for host mode */
			gpio_set_flags(GPIO_USB_C1_HOST_HIGH, GPIO_OUTPUT);
			gpio_set_level(GPIO_USB_C1_HOST_HIGH, 1);
			/* High-Z is used for host mode. */
			gpio_set_level(GPIO_USB_C1_CC1_ODL, 1);
			gpio_set_level(GPIO_USB_C1_CC2_ODL, 1);
			/* Set TX Hi-Z */
			gpio_set_flags(GPIO_USB_C1_CCX_TX_DATA, GPIO_INPUT);
		} else {
			/* Set HOST_HIGH to High-Z for device mode. */
			gpio_set_flags(GPIO_USB_C1_HOST_HIGH, GPIO_INPUT);
			/* Pull low for device mode. */
			gpio_set_level(GPIO_USB_C1_CC1_ODL, 0);
			gpio_set_level(GPIO_USB_C1_CC2_ODL, 0);
		}
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

	if (port == 0) {
			gpio_set_level(GPIO_USB_C0_CC1_VCONN1_EN, 0);
			gpio_set_level(GPIO_USB_C0_CC2_VCONN1_EN, 0);
	} else {
			gpio_set_level(GPIO_USB_C1_CC1_VCONN1_EN, 0);
			gpio_set_level(GPIO_USB_C1_CC2_VCONN1_EN, 0);
	}
}

static inline int pd_adc_read(int port, int cc)
{
	if (port == 0)
		return adc_read_channel(cc ? ADC_C0_CC2_PD : ADC_C0_CC1_PD);
	else
		return adc_read_channel(cc ? ADC_C1_CC2_PD : ADC_C1_CC1_PD);
}

static inline void pd_set_vconn(int port, int polarity, int enable)
{
	/* Set VCONN on the opposite CC line from the polarity */
	if (port == 0) {
		gpio_set_level(polarity ? GPIO_USB_C0_CC1_VCONN1_EN :
					  GPIO_USB_C0_CC2_VCONN1_EN, enable);
		/* Set TX_DATA pin to Hi-Z */
		gpio_set_flags(polarity	? GPIO_USB_C0_CC1_TX_DATA :
					  GPIO_USB_C0_CC2_TX_DATA, GPIO_INPUT);
	} else {
		gpio_set_level(polarity ? GPIO_USB_C1_CC1_VCONN1_EN :
					  GPIO_USB_C1_CC2_VCONN1_EN, enable);
		/* Set TX_DATA pin to Hi-Z */
		gpio_set_flags(GPIO_USB_C1_CCX_TX_DATA, GPIO_INPUT);
	}
}

#endif /* __USB_PD_CONFIG_H */

