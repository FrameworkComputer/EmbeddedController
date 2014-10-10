/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fruitpie board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_USB
#define CONFIG_USB_MS
#define CONFIG_USB_MS_BUFFER_SIZE SPI_FLASH_MAX_WRITE_SIZE
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USBC_SS_MUX
#define CONFIG_ADC
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_BATTERY_SMART
#define CONFIG_USB_SWITCH_TSU6721
#define CONFIG_SPI_FLASH
#define CONFIG_SPI_FLASH_SIZE 8388608
#define CONFIG_SPI_MASTER_PORT 2
#define CONFIG_SPI_CS_GPIO GPIO_PD_TX_EN
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/* I2C ports configuration */
#define I2C_PORT_MASTER 1
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_SLAVE  0

/* Charger configuration */
#define CONFIG_CHARGER
#undef  CONFIG_CHARGER_V1
#define CONFIG_CHARGER_BQ24773
#define CONFIG_CHARGER_SENSE_RESISTOR     5 /* milliOhms */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10 /* milliOhms */
#define CONFIG_CHARGER_INPUT_CURRENT    512 /* mA */
#define CONFIG_CHARGER_ILIM_PIN_DISABLED    /* external ILIM pin disabled */

/* USB configuration */
#define CONFIG_USB_PID 0x5009
/* By default, enable all console messages excepted USB */
#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_USB))

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/*
 * Timeout to wait for TSU6721 to detect power. Set to double the BCD timer.
 */
#define DEBUG_SWITCH_TIMEOUT_MSEC (1200*MSEC)

/*
 * Used to set GPIO's and clock to SPI module used for debug
 *
 * @param enable Whether to enable or disable debug
 */
int board_set_debug(int enable);

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_CH_CC1_PD = 0,
	ADC_CH_CC2_PD,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_MS		0
#define USB_IFACE_COUNT		1

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_MS_TX		1
#define USB_EP_MS_RX		2
#define USB_EP_COUNT		3

#endif /* __BOARD_H */
