/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* NPCX9 config */
#define NPCX9_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

/* Config options automatically enabled, re-enable once support added */
#undef CONFIG_ADC
#undef CONFIG_SWITCH

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* EC Config Defines */
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_VOLUME_BUTTONS

/* See config_chip-npcx9.h for SPI flash configuration */
#undef CONFIG_SPI_FLASH /* Don't enable external flash interface */

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_WP_L			GPIO_EC_WP_L
#define GPIO_POWER_BUTTON_L		GPIO_MECH_PWR_BTN_ODL
#define GPIO_AC_PRESENT			GPIO_ACOK_OD
#define GPIO_SYS_RESET_L		GPIO_EC_SYS_RST_L
#define GPIO_PCH_PWRBTN_L		GPIO_EC_SOC_PWR_BTN_L
#define GPIO_VOLUME_UP_L		GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L		GPIO_VOLDN_BTN_ODL

/* Host communication */

/* Chipset config */

/* Common Keyboard Defines */

/* Sensors */
#define CONFIG_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE
#define GMR_TABLET_MODE_GPIO_L		GPIO_TABLET_MODE

/* Common charger defines */

/* Common battery defines */

/* USB Type C and USB PD defines */
#define CONFIG_IO_EXPANDER_PORT_COUNT USBC_PORT_COUNT

/* USB Type A Features */

/* BC 1.2 */

/* I2C Bus Configuration */

/* Volume Button feature */

/* Fan features */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void sbu_fault_interrupt(enum ioex_signal signal);

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
