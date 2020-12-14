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

/* Power Config */
#undef  CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define GPIO_AC_PRESENT		GPIO_ACOK_OD
#define GPIO_POWER_BUTTON_L	GPIO_MECH_PWR_BTN_ODL
#define GPIO_PCH_PWRBTN_L	GPIO_EC_SOC_PWR_BTN_L
#define GPIO_PCH_RSMRST_L	GPIO_EC_SOC_RSMRST_L
#define GPIO_PCH_WAKE_L		GPIO_EC_SOC_WAKE_L
#define GPIO_PCH_SLP_S0_L	GPIO_SLP_S3_S0I3_L
#define GPIO_PCH_SLP_S3_L	GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S5_L	GPIO_SLP_S5_L
#define GPIO_S0_PGOOD		GPIO_S0_PWROK_OD
#define GPIO_S5_PGOOD		GPIO_S5_PWROK
#define GPIO_PCH_SYS_PWROK	GPIO_EC_SOC_PWR_GOOD
#define GPIO_SYS_RESET_L	GPIO_EC_SYS_RST_L
#define GPIO_EN_PWR_A		GPIO_EN_PWR_Z1

/* Thermal Config */
#define GPIO_CPU_PROCHOT	GPIO_PROCHOT_ODL

/* Flash Config */
/* See config_chip-npcx9.h for SPI flash configuration */
#undef CONFIG_SPI_FLASH /* Don't enable external flash interface */
#define GPIO_WP_L			GPIO_EC_WP_L

/* Host communication */

/* Chipset config */
#define CONFIG_CHIPSET_STONEY
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_RESET_HOOK

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
#define CONFIG_I2C
#define CONFIG_I2C_BUS_MAY_BE_UNPOWERED
#define CONFIG_I2C_CONTROLLER
#define CONFIG_I2C_UPDATE_IF_CHANGED
#define I2C_PORT_TCPC0		NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC1		NPCX_I2C_PORT1_0
#define I2C_PORT_BATTERY	NPCX_I2C_PORT2_0
#define I2C_PORT_USB_MUX	NPCX_I2C_PORT3_0
#define I2C_PORT_POWER		NPCX_I2C_PORT4_1
#define I2C_PORT_CHARGER	I2C_PORT_POWER
#define I2C_PORT_EEPROM		NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR		NPCX_I2C_PORT6_1
#define I2C_PORT_SOC_THERMAL	NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM_FLAGS	0x50

/* Volume Button Config */
#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L		GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L		GPIO_VOLDN_BTN_ODL

/* Fan features */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Power input signals */
enum power_signal {
	X86_SLP_S0_N,		/* SOC  -> SLP_S3_S0I3_L */
	X86_SLP_S3_N,		/* SOC  -> SLP_S3_L */
	X86_SLP_S5_N,		/* SOC  -> SLP_S5_L */

	X86_S0_PGOOD,		/* PMIC -> S0_PWROK_OD */
	X86_S5_PGOOD,		/* PMIC -> S5_PWROK */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT,
};

/* USB-C ports */
enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void sbu_fault_interrupt(enum ioex_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
