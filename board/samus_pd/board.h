/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* samus_pd board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_STM_HWTIMER32
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_ADC
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_USB_SWITCH_TSU6721
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/* I2C ports configuration */
#define I2C_PORT_MASTER 1
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_SLAVE  0

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_USB_C0_VBUS_WAKE = 0,
	GPIO_USB_C1_VBUS_WAKE,
	GPIO_USB_C0_BC12_INT_L,
	GPIO_USB_C1_BC12_INT_L,

	/* PD RX/TX */
	GPIO_USB_C0_CC1_PD,
	GPIO_USB_C0_REF,
	GPIO_USB_C1_CC1_PD,
	GPIO_USB_C1_REF,
	GPIO_USB_C0_CC2_PD,
	GPIO_USB_C1_CC2_PD,
	GPIO_USB_C0_REF_PD_ODL,
	GPIO_USB_C1_REF_PD_ODL,

	GPIO_USB_C_CC_EN,
	GPIO_USB_C0_CC1_TX_EN,
	GPIO_USB_C0_CC2_TX_EN,
	GPIO_USB_C1_CC1_TX_EN,
	GPIO_USB_C1_CC2_TX_EN,

#if 0
	/* Alternate functions */
	GPIO_USB_C0_TX_CLKOUT,
	GPIO_USB_C1_TX_CLKOUT,
	GPIO_USB_C0_TX_CLKIN,
	GPIO_USB_C1_TX_CLKIN,

	GPIO_USB_C0_CC1_TX_DATA,
	GPIO_USB_C0_CC1_TX_DATA,
	GPIO_USB_C1_CC1_TX_DATA,
	GPIO_USB_C1_CC1_TX_DATA,
	GPIO_USB_C0_CC2_TX_DATA,
	GPIO_USB_C0_CC2_TX_DATA,
	GPIO_USB_C1_CC2_TX_DATA,
	GPIO_USB_C1_CC2_TX_DATA,
#endif

	/* Power and muxes control */
	GPIO_PP3300_USB_PD_EN,
	GPIO_USB_C0_CHARGE_EN_L,
	GPIO_USB_C1_CHARGE_EN_L,
	GPIO_USB_C0_5V_EN,
	GPIO_USB_C1_5V_EN,
	GPIO_USB_C0_VCONN1_EN,
	GPIO_USB_C0_VCONN2_EN,
	GPIO_USB_C1_VCONN1_EN,
	GPIO_USB_C1_VCONN2_EN,

	GPIO_USB_C0_CC1_ODL,
	GPIO_USB_C0_CC2_ODL,
	GPIO_USB_C1_CC1_ODL,
	GPIO_USB_C1_CC2_ODL,

	GPIO_USB_C_BC12_SEL,

	GPIO_USB_C0_SS1_EN_L,
	GPIO_USB_C0_SS2_EN_L,
	GPIO_USB_C1_SS1_EN_L,
	GPIO_USB_C1_SS2_EN_L,
	GPIO_USB_C0_SS1_DP_MODE_L,
	GPIO_USB_C0_SS2_DP_MODE_L,
	GPIO_USB_C1_SS1_DP_MODE_L,
	GPIO_USB_C1_SS2_DP_MODE_L,
	GPIO_USB_C0_DP_MODE_L,
	GPIO_USB_C1_DP_MODE_L,
	GPIO_USB_C0_DP_POLARITY_L,
	GPIO_USB_C1_DP_POLARITY_L,

#if 0
	/* Alternate functions */
	GPIO_USB_DM,
	GPIO_USB_DP,
	GPIO_UART_TX,
	GPIO_UART_RX,
	GPIO_TP64,
	GPIO_TP71,
#endif

	/* I2C busses */
	GPIO_SLAVE_I2C_SCL,
	GPIO_SLAVE_I2C_SDA,
	GPIO_MASTER_I2C_SCL,
	GPIO_MASTER_I2C_SDA,

	/* Test points */
	GPIO_TP60,

	/* Case closed debugging */
	GPIO_SPI_FLASH_WP_L,
	GPIO_EC_INT_L,
	GPIO_EC_IN_RW,
	GPIO_EC_RST_L,
	GPIO_SPI_FLASH_CS_L,
	GPIO_SPI_FLASH_CSK,
	GPIO_SPI_FLASH_MOSI,
	GPIO_SPI_FLASH_MISO,
	GPIO_EC_JTAG_TMS,
	GPIO_EC_JTAG_TCK,
	GPIO_EC_JTAG_TDO,
	GPIO_EC_JTAT_TDI,
#if 0
	/* Alternate functions */
	GPIO_EC_UART_TX,
	GPIO_EC_UART_RX,
	GPIO_AP_UART_TX,
	GPIO_AP_UART_RX,
#endif

	/* Unimplemented signals we emulate */
	GPIO_ENTERING_RW,
	GPIO_WP_L,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* ADC signal */
enum adc_channel {
	ADC_C0_CC1_PD = 0,
	ADC_C0_CC2_PD,
	ADC_C1_CC1_PD,
	ADC_C1_CC2_PD,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* Muxing for the USB type C */
enum typec_mux {
	TYPEC_MUX_NONE,
	TYPEC_MUX_USB1,
	TYPEC_MUX_USB2,
	TYPEC_MUX_DP1,
	TYPEC_MUX_DP2,
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
