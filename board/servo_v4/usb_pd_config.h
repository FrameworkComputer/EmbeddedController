/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "chip/stm32/registers.h"
#include "console.h"
#include "gpio.h"
#include "ec_commands.h"
#include "usb_pd_tcpm.h"

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

/* NOTES: Servo V4 and glados equivalents:
 *	Glados		Servo V4
 *	C0		CHG
 *	C1		DUT
 *
 */
#define CHG 0
#define DUT 1

/* Timer selection for baseband PD communication */
#define TIM_CLOCK_PD_TX_CHG 16
#define TIM_CLOCK_PD_RX_CHG 1
#define TIM_CLOCK_PD_TX_DUT 15
#define TIM_CLOCK_PD_RX_DUT 3

/* Timer channel */
#define TIM_TX_CCR_CHG 1
#define TIM_RX_CCR_CHG 1
#define TIM_TX_CCR_DUT 2
#define TIM_RX_CCR_DUT 1

#define TIM_CLOCK_PD_TX(p) ((p) ? TIM_CLOCK_PD_TX_DUT : TIM_CLOCK_PD_TX_CHG)
#define TIM_CLOCK_PD_RX(p) ((p) ? TIM_CLOCK_PD_RX_DUT : TIM_CLOCK_PD_RX_CHG)

/* RX timer capture/compare register */
#define TIM_CCR_CHG (&STM32_TIM_CCRx(TIM_CLOCK_PD_RX_CHG, TIM_RX_CCR_CHG))
#define TIM_CCR_DUT (&STM32_TIM_CCRx(TIM_CLOCK_PD_RX_DUT, TIM_RX_CCR_DUT))
#define TIM_RX_CCR_REG(p) ((p) ? TIM_CCR_DUT : TIM_CCR_CHG)

/* TX and RX timer register */
#define TIM_REG_TX_CHG (STM32_TIM_BASE(TIM_CLOCK_PD_TX_CHG))
#define TIM_REG_RX_CHG (STM32_TIM_BASE(TIM_CLOCK_PD_RX_CHG))
#define TIM_REG_TX_DUT (STM32_TIM_BASE(TIM_CLOCK_PD_TX_DUT))
#define TIM_REG_RX_DUT (STM32_TIM_BASE(TIM_CLOCK_PD_RX_DUT))
#define TIM_REG_TX(p) ((p) ? TIM_REG_TX_DUT : TIM_REG_TX_CHG)
#define TIM_REG_RX(p) ((p) ? TIM_REG_RX_DUT : TIM_REG_RX_CHG)

/* use the hardware accelerator for CRC */
#define CONFIG_HW_CRC

/* Servo v4 CC configuration */
#define CC_DETACH	(1 << 0)   /* Emulate detach: both CC open */
#define CC_DISABLE_DTS	(1 << 1)   /* Apply resistors to single or both CC? */
#define CC_ALLOW_SRC	(1 << 2)   /* Allow charge through by policy? */
#define CC_ENABLE_DRP	(1 << 3)   /* Enable dual-role port */
#define CC_SNK_WITH_PD	(1 << 4)   /* Force enabling PD comm for sink role */
#define CC_POLARITY	(1 << 5)   /* CC polarity */

/* TX uses SPI1 on PB3-4 for CHG port, SPI2 on PB 13-14 for DUT port */
#define SPI_REGS(p) ((p) ? STM32_SPI2_REGS : STM32_SPI1_REGS)
static inline void spi_enable_clock(int port)
{
	if (port == 0)
		STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;
	else
		STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;
}

/* DMA for transmit uses DMA CH3 for CHG and DMA_CH7 for DUT */
#define DMAC_SPI_TX(p) ((p) ? STM32_DMAC_CH7 : STM32_DMAC_CH3)

/* RX uses COMP1 and TIM1_CH1 on port CHG and COMP2 and TIM3_CH1 for port DUT*/
/* DUT RX use CMP1, TIM3_CH1, DMA_CH6 */
#define CMP1OUTSEL STM32_COMP_CMP1OUTSEL_TIM3_IC1
/* CHG RX use CMP2, TIM1_CH1, DMA_CH2 */
#define CMP2OUTSEL STM32_COMP_CMP2OUTSEL_TIM1_IC1

#define TIM_TX_CCR_IDX(p) ((p) ? TIM_TX_CCR_DUT : TIM_TX_CCR_CHG)
#define TIM_RX_CCR_IDX(p) ((p) ? TIM_RX_CCR_DUT : TIM_RX_CCR_CHG)
#define TIM_CCR_CS  1

/*
 * EXTI line 21 is connected to the CMP1 output,
 * EXTI line 22 is connected to the CMP2 output,
 * CHG uses CMP2, and DUT uses CMP1.
 */
#define EXTI_COMP_MASK(p) ((p) ? (1<<21) : BIT(22))

#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* DMA for receive uses DMA_CH2 for CHG and DMA_CH6 for DUT */
#define DMAC_TIM_RX(p) ((p) ? STM32_DMAC_CH6 : STM32_DMAC_CH2)

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	if (port == 0) {
		/* 40 MHz pin speed on SPI PB3&4,
		 * (USB_CHG_TX_CLKIN & USB_CHG_CC1_TX_DATA)
		 */
		STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000003C0;
		/* 40 MHz pin speed on TIM16_CH1 (PB8),
		 * (USB_CHG_TX_CLKOUT)
		 */
		STM32_GPIO_OSPEEDR(GPIO_B) |= 0x00030000;
	} else {
		/* 40 MHz pin speed on SPI PB13/14,
		 * (USB_DUT_TX_CLKIN & USB_DUT_CC1_TX_DATA)
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
		STM32_RCC_APB2RSTR |= BIT(12);
		STM32_RCC_APB2RSTR &= ~BIT(12);
	} else {
		/* Reset SPI2 */
		STM32_RCC_APB1RSTR |= BIT(14);
		STM32_RCC_APB1RSTR &= ~BIT(14);
	}
}

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int port, int polarity)
{
	if (port == 0) {
		/* put SPI function on TX pin */
		if (polarity) {
			const struct gpio_info *g = gpio_list +
				GPIO_USB_CHG_CC2_TX_DATA;
			gpio_set_alternate_function(g->port, g->mask, 0);

			/* set the low level reference */
			gpio_set_flags(GPIO_USB_CHG_CC2_PD, GPIO_OUT_LOW);
		} else {
			const struct gpio_info *g = gpio_list +
				GPIO_USB_CHG_CC1_TX_DATA;
			gpio_set_alternate_function(g->port, g->mask, 0);

			/* set the low level reference */
			gpio_set_flags(GPIO_USB_CHG_CC1_PD, GPIO_OUT_LOW);
		}
	} else {
		/* put SPI function on TX pin */
		/* MCU ADC pin output low */
		if (polarity) {
			/* USB_DUT_CC2_TX_DATA: PC2 is SPI2 MISO */
			const struct gpio_info *g = gpio_list +
				GPIO_USB_DUT_CC2_TX_DATA;
			gpio_set_alternate_function(g->port, g->mask, 1);

			/* set the low level reference */
			gpio_set_flags(GPIO_USB_DUT_CC2_PD, GPIO_OUT_LOW);
		} else {
			/* USB_DUT_CC1_TX_DATA: PB14 is SPI2 MISO */
			const struct gpio_info *g = gpio_list +
				GPIO_USB_DUT_CC1_TX_DATA;
			gpio_set_alternate_function(g->port, g->mask, 0);

			/* set the low level reference */
			gpio_set_flags(GPIO_USB_DUT_CC1_PD, GPIO_OUT_LOW);
		}
	}
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	if (port == 0) {
		if (polarity) {
			gpio_set_flags(GPIO_USB_CHG_CC2_TX_DATA, GPIO_INPUT);
			gpio_set_flags(GPIO_USB_CHG_CC2_PD, GPIO_ANALOG);
		} else {
			gpio_set_flags(GPIO_USB_CHG_CC1_TX_DATA, GPIO_INPUT);
			gpio_set_flags(GPIO_USB_CHG_CC1_PD, GPIO_ANALOG);
		}
	} else {
		if (polarity) {
			gpio_set_flags(GPIO_USB_DUT_CC2_TX_DATA, GPIO_INPUT);
			gpio_set_flags(GPIO_USB_DUT_CC2_PD, GPIO_ANALOG);
		} else {
			gpio_set_flags(GPIO_USB_DUT_CC1_TX_DATA, GPIO_INPUT);
			gpio_set_flags(GPIO_USB_DUT_CC1_PD, GPIO_ANALOG);
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
		/* CHG use the right comparator inverted input for COMP2 */
		STM32_COMP_CSR = (val & ~STM32_COMP_CMP2INSEL_MASK) |
			(polarity ? STM32_COMP_CMP2INSEL_INM4  /* PA4: C0_CC2 */
				  : STM32_COMP_CMP2INSEL_INM6);/* PA2: C0_CC1 */
	} else {
		/* DUT use the right comparator inverted input for COMP1 */
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
	/*
	 * CHG (port == 0) port has fixed Rd attached and therefore can only
	 * present as a SNK device. If port != DUT (port == 1), then nothing to
	 * do in this function.
	 */
	if (!port)
		return;

	if (enable) {
		/*
		 * Servo_v4 in SRC mode acts as a DTS (debug test
		 * accessory) and needs to present Rp on both CC
		 * lines. In order to support orientation detection, and
		 * advertise the correct TypeC current level, the
		 * values of Rp1/Rp2 need to asymmetric with Rp1 > Rp2. This
		 * function is called without a specified Rp value so assume the
		 * servo_v4 default of USB level current. If a higher current
		 * can be supported, then the Rp value will get adjusted when
		 * VBUS is enabled.
		 */
		pd_set_rp_rd(port, TYPEC_CC_RP, TYPEC_RP_USB);

		gpio_set_flags(GPIO_USB_DUT_CC1_TX_DATA, GPIO_INPUT);
		gpio_set_flags(GPIO_USB_DUT_CC2_TX_DATA, GPIO_INPUT);
	} else {
		/* Select Rd, the Rp value is a don't care */
		pd_set_rp_rd(port, TYPEC_CC_RD, TYPEC_RP_RESERVED);
	}
}

/**
 * Initialize various GPIOs and interfaces to safe state at start of pd_task.
 *
 * These include:
 *   VBUS, charge path based on power role.
 *   Physical layer CC transmit.
 *
 * @param port        USB-C port number
 * @param power_role  Power role of device
 */
static inline void pd_config_init(int port, uint8_t power_role)
{
	/*
	 * Set CC pull resistors. The PD state machine will then transit and
	 * enable VBUS after it detects valid voltages on CC lines.
	 */
	pd_set_host_mode(port, power_role);

	/* Initialize TX pins and put them in Hi-Z */
	pd_tx_init();

}

int pd_adc_read(int port, int cc);

#endif /* __CROS_EC_USB_PD_CONFIG_H */

