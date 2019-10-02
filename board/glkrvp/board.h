/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel GLK-RVP board-specific configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* EC console commands  */

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_SMART

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_CHARGER_PROFILE_OVERRIDE_COMMON
#undef  CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES
#define CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES 3
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#undef  CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 1000
#define CONFIG_EXTPOWER_GPIO

/* DC Jack charge ports */
#undef  CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1
#define DEDICATED_CHARGE_PORT 2

/* Keyboard */
#define CONFIG_KEYBOARD_PROTOCOL_8042

/* UART */
#define NPCX_UART_MODULE2    1 /* 0:GPIO10/11 1:GPIO64/65 as UART */
#define NPCX7_PWM1_SEL       0 /* GPIO C2 is not used as PWM1. */

/* USB-A config */

/* USB PD config */
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_POWER_DELIVERY

/* USB MUX */
#define CONFIG_USBC_SS_MUX
#define CONFIG_USB_MUX_PS8743

/* SoC / PCH */
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_CHIPSET_GEMINILAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* EC */
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL
#define CONFIG_WP_ALWAYS
#define CONFIG_FLASH_READOUT_PROTECTION
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

#define CONFIG_LID_SWITCH
#define CONFIG_LTO

#define CONFIG_LOW_POWER_IDLE

#define CONFIG_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q40

/* Verified boot */
#define CONFIG_SHA256_UNROLLED
#define CONFIG_VBOOT_HASH
/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Optional feature - used by nuvoton */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
#define NPCX_TACH_SEL2       0 /* 0:GPIO40/A4 1:GPIO93/D3 as TACH */

/* I2C ports */
#define I2C_PORT_CHARGER	NPCX_I2C_PORT3_0
#define I2C_PORT_BATTERY	NPCX_I2C_PORT3_0
#define I2C_PORT_USB_MUX	NPCX_I2C_PORT7_0

/* EC exclude modules */
#undef CONFIG_ADC

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_VBUS,
	ADC_CH_COUNT,
};

int board_get_version(void);

/* TODO: Verify the numbers below. */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW  15000
#define PD_MAX_POWER_MW        45000
#define PD_MAX_CURRENT_MA      3000
#define PD_MAX_VOLTAGE_MV      20000
#define DC_JACK_MAX_VOLTAGE_MV 19000

/* Reset PD MCU */
void board_reset_pd_mcu(void);
void tcpc_alert_event(enum gpio_signal signal);
void board_charging_enable(int port, int enable);
void board_vbus_enable(int port, int enable);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
