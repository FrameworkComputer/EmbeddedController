/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "chip/stm32/registers.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "usb_pd_tcpm.h"

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

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
#define CC_DETACH BIT(0) /* Emulate detach: both CC open */
#define CC_DISABLE_DTS BIT(1) /* Apply resistors to single or both CC? */
#define CC_ALLOW_SRC BIT(2) /* Allow charge through by policy? */
#define CC_ENABLE_DRP BIT(3) /* Enable dual-role port */
#define CC_SNK_WITH_PD BIT(4) /* Force enabling PD comm for sink role */
#define CC_POLARITY BIT(5) /* CC polarity */
#define CC_EMCA_SERVO                                          \
	BIT(6) /*                                              \
		* Emulate Electronically Marked Cable Assembly \
		* (EMCA) servo (or non-EMCA)                   \
		*/
#define CC_FASTBOOT_DFP BIT(7) /* Allow mux uServo->Fastboot on DFP */

/* Servo v4 DP alt-mode configuration */
#define ALT_DP_ENABLE BIT(0) /* Enable DP alt-mode or not */
#define ALT_DP_PIN_C BIT(1) /* Pin assignment C supported */
#define ALT_DP_PIN_D BIT(2) /* Pin assignment D supported */
#define ALT_DP_MF_PREF BIT(3) /* Multi-Function preferred */
#define ALT_DP_PLUG BIT(4) /* Plug or receptacle */
#define ALT_DP_OVERRIDE_HPD BIT(5) /* Override the HPD signal */
#define ALT_DP_HPD_LVL BIT(6) /* HPD level if overridden */

/* TX uses SPI1 on PB3-4 for CHG port, SPI2 on PB 13-14 for DUT port */
#define SPI_REGS(p) ((p) ? STM32_SPI2_REGS : STM32_SPI1_REGS)
static inline void spi_enable_clock(int port)
{
	if (port == CHG)
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
#define TIM_CCR_CS 1

/*
 * EXTI line 21 is connected to the CMP1 output,
 * EXTI line 22 is connected to the CMP2 output,
 * CHG uses CMP2, and DUT uses CMP1.
 */
#define EXTI_COMP_MASK(p) ((p) ? (1 << 21) : BIT(22))

#define IRQ_COMP STM32_IRQ_COMP
/* triggers packet detection on comparator falling edge */
#define EXTI_XTSR STM32_EXTI_FTSR

/* DMA for receive uses DMA_CH2 for CHG and DMA_CH6 for DUT */
#define DMAC_TIM_RX(p) ((p) ? STM32_DMAC_CH6 : STM32_DMAC_CH2)

/* the pins used for communication need to be hi-speed */
static inline void pd_set_pins_speed(int port)
{
	if (port == CHG) {
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
	if (port == CHG) {
		/* Reset SPI1 */
		STM32_RCC_APB2RSTR |= BIT(12);
		STM32_RCC_APB2RSTR &= ~BIT(12);
	} else {
		/* Reset SPI2 */
		STM32_RCC_APB1RSTR |= BIT(14);
		STM32_RCC_APB1RSTR &= ~BIT(14);
	}
}

static const uint8_t tx_gpio[2 /* port */][2 /* polarity */] = {
	{ GPIO_USB_CHG_CC1_TX_DATA, GPIO_USB_CHG_CC2_TX_DATA },
	{ GPIO_USB_DUT_CC1_TX_DATA, GPIO_USB_DUT_CC2_TX_DATA },
};
static const uint8_t ref_gpio[2 /* port */][2 /* polarity */] = {
	{ GPIO_USB_CHG_CC1_MCU, GPIO_USB_CHG_CC2_MCU },
	{ GPIO_USB_DUT_CC1_MCU, GPIO_USB_DUT_CC2_MCU },
};

/* Drive the CC line from the TX block */
static inline void pd_tx_enable(int port, int polarity)
{
#ifndef VIF_BUILD /* genvif doesn't like tricks with GPIO macros */
	const struct gpio_info *tx = gpio_list + tx_gpio[port][polarity];
	const struct gpio_info *ref = gpio_list + ref_gpio[port][polarity];

	/* use directly GPIO registers, latency before the PD preamble is key */

	/* switch the TX pin Mode from Input (00) to Alternate (10) for SPI */
	STM32_GPIO_MODER(tx->port) |= 2 << ((31 - __builtin_clz(tx->mask)) * 2);
	/* switch the ref pin Mode from analog (11) to Out (01) for low level */
	STM32_GPIO_MODER(ref->port) &=
		~(2 << ((31 - __builtin_clz(ref->mask)) * 2));
#endif /* !VIF_BUILD */
}

/* Put the TX driver in Hi-Z state */
static inline void pd_tx_disable(int port, int polarity)
{
	const struct gpio_info *tx = gpio_list + tx_gpio[port][polarity];
	const struct gpio_info *ref = gpio_list + ref_gpio[port][polarity];

	gpio_set_flags_by_mask(tx->port, tx->mask, GPIO_INPUT);
	gpio_set_flags_by_mask(ref->port, ref->mask, GPIO_ANALOG);
}

/* we know the plug polarity, do the right configuration */
static inline void pd_select_polarity(int port, int polarity)
{
	uint32_t val = STM32_COMP_CSR;

	/* Use window mode so that COMP1 and COMP2 share non-inverting input */
	val |= STM32_COMP_CMP1EN | STM32_COMP_CMP2EN | STM32_COMP_WNDWEN;

	if (port == CHG) {
		/* CHG use the right comparator inverted input for COMP2 */
		STM32_COMP_CSR = (val & ~STM32_COMP_CMP2INSEL_MASK) |
				 (polarity ?
					  STM32_COMP_CMP2INSEL_INM4 /* PA4:
								       C0_CC2
								     */
					  :
					  STM32_COMP_CMP2INSEL_INM6); /* PA2:
									 C0_CC1
								       */
	} else {
		/* DUT use the right comparator inverted input for COMP1 */
		STM32_COMP_CSR = (val & ~STM32_COMP_CMP1INSEL_MASK) |
				 (polarity ?
					  STM32_COMP_CMP1INSEL_INM5 /* PA5:
								       C1_CC2
								     */
					  :
					  STM32_COMP_CMP1INSEL_INM6); /* PA0:
									 C1_CC1
								       */
	}
}

/* Initialize pins used for TX and put them in Hi-Z */
static inline void pd_tx_init(void)
{
	const struct gpio_info *c2 = gpio_list + GPIO_USB_CHG_CC2_TX_DATA;
	const struct gpio_info *c1 = gpio_list + GPIO_USB_CHG_CC1_TX_DATA;
	const struct gpio_info *d2 = gpio_list + GPIO_USB_DUT_CC2_TX_DATA;
	const struct gpio_info *d1 = gpio_list + GPIO_USB_DUT_CC1_TX_DATA;

	gpio_config_module(MODULE_USB_PD, 1);
	/* Select the proper alternate SPI function on TX_DATA pins */
	/* USB_CHG_CC2_TX_DATA: PA6 is SPI1 MISO (AF0) */
	gpio_set_alternate_function(c2->port, c2->mask, 0);
	gpio_set_flags_by_mask(c2->port, c2->mask, GPIO_INPUT);
	/* USB_CHG_CC1_TX_DATA: PB4 is SPI1 MISO (AF0) */
	gpio_set_alternate_function(c1->port, c1->mask, 0);
	gpio_set_flags_by_mask(c1->port, c1->mask, GPIO_INPUT);
	/* USB_DUT_CC2_TX_DATA: PC2 is SPI2 MISO (AF1) */
	gpio_set_alternate_function(d2->port, d2->mask, 1);
	gpio_set_flags_by_mask(d2->port, d2->mask, GPIO_INPUT);
	/* USB_DUT_CC1_TX_DATA: PB14 is SPI2 MISO (AF0) */
	gpio_set_alternate_function(d1->port, d1->mask, 0);
	gpio_set_flags_by_mask(d1->port, d1->mask, GPIO_INPUT);
}

static inline void pd_set_host_mode(int port, int enable)
{
	/*
	 * CHG (port == 0) port has fixed Rd attached and therefore can only
	 * present as a SNK device. If port != DUT (port == 1), then nothing to
	 * do in this function.
	 */
	if (port != DUT)
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

/**
 * External function to allow setting or clearing specific flags in cc_config.
 * Allows similar functionality as cc console command.
 *
 * @param flag        cc_config flag to set/clear
 * @param set         true to set, false to clear flag
 */
void set_cc_flag(int flag, bool set);

#endif /* __CROS_EC_USB_PD_CONFIG_H */
