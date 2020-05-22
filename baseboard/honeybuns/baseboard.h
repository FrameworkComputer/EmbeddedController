/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* EC Defines */
#define CONFIG_CRC8

/* TODO Define FLASH_PSTATE_LOCKED prior to building MP FW. */
#undef CONFIG_FLASH_PSTATE_LOCKED

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000
#define CONFIG_STM_HWTIMER32
#define TIM_CLOCK32 2
#define TIM_CLOCK_MSB  3
#define TIM_CLOCK_LSB 15
#define TIM_WATCHDOG 7

/* Honeybuns platform does not have a lid switch */
#undef CONFIG_LID_SWITCH

/* USART and EC console configs */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 3
#define CONFIG_UART_TX_DMA
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART3_TX
#define CONFIG_UART_TX_DMA_PH DMAMUX_REQ_USART3_TX

/* CBI Configs */
#define I2C_ADDR_EEPROM_FLAGS   0x50
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CMD_CBI


/* USB Type C and USB PD defines */

/* BC 1.2 */

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_USBC		0
#define I2C_PORT_MST		1
#define I2C_PORT_EEPROM	2


/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_ENTERING_RW	GPIO_EC_ENTERING_RW
#define GPIO_WP_L		GPIO_EC_WP_L

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "stddef.h"

struct power_seq {
	enum gpio_signal signal; /* power/reset gpio_signal to control */
	int level;               /* level to set in power sequence */
	unsigned int delay_ms;   /* delay (in msec) after setting gpio_signal */
};

extern const struct power_seq board_power_seq[];
extern const size_t board_power_seq_count;

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
