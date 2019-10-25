/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H


/* NPCX7 config */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Defines */
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_HIBERNATE_PSL
#define CONFIG_LED_COMMON
/* TODO(b/140557020): Define CONFIG_LED_ONOFF_STATES and
 * CONFIG_LED_ONOFF_STATES_BAT_LOW when CONFIG_CHARGER is defined.
 */
#define CONFIG_LED_PWM
/* TODO(b/140557020): Remove this when CONFIG_CHARGER is defined. */
#define CONFIG_LED_PWM_CHARGE_STATE_ONLY
/* Although there are 2 LEDs, they are both controlled by the same lines. */
#define CONFIG_LED_PWM_COUNT 1
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Host communication */
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S4

/* Chipset config */
#define CONFIG_CHIPSET_TIGERLAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S0IX_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_BOARD_HAS_RTC_RESET

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_KEYPAD
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2

/* Sensors */

/* Common charger defines */

/* Common battery defines */

/* USB Type C and USB PD defines */

/* BC 1.2 */

/* I2C Bus Configuration */
#define CONFIG_I2C
#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0		NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1		NPCX_I2C_PORT2_0
#define I2C_PORT_USB_1_MIX	NPCX_I2C_PORT3_0
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM_FLAGS	0x50
#define CONFIG_I2C_MASTER


#ifndef __ASSEMBLER__

enum pwm_channel {
	PWM_CH_LED1_BLUE = 0,
	PWM_CH_LED2_GREEN,
	PWM_CH_LED3_RED,
	PWM_CH_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
