/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* NPCX9 config */
#define NPCX9_PWM1_SEL 1 /* GPIO C2 is used as PWM1. */
/*
 * This defines which pads (GPIO10/11 or GPIO64/65) are connected to
 * the "UART1" (NPCX_UART_PORT0) controller when used for
 * CONSOLE_UART.
 */
#define NPCX_UART_MODULE2 1 /* 1:GPIO64/65 for UART1 */

/* EC Defines */
#define CONFIG_LTO
#define CONFIG_CBI_EEPROM
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8
#define CONFIG_DPTF
#define CONFIG_FPU

/* Verified boot configs */
#define CONFIG_VBOOT_EFS2
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

#define CONFIG_HIBERNATE_PSL

/* Work around double CR50 reset by waiting in initial power on. */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

/* Host communication */
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S5

/*
 * TODO(b/179648721): implement sensors
 */
#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

#define CONFIG_MKBP_EVENT
/* GPIO is needed for EC events to show up in eventlog - b/222375516 */
#define CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT
#define CONFIG_MKBP_INPUT_DEVICES

/* LED */
#define CONFIG_LED_COMMON

/* Common charger defines */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512

#define CONFIG_CMD_CHARGER_DUMP

#define CONFIG_USB_CHARGER
#define CONFIG_BC12_DETECT_PI3USB9201

/*
 * Don't allow the system to boot to S0 when the battery is low and unable to
 * communicate on locked systems (which haven't PD negotiated)
 */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT 15000
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15001

/* Common battery defines */
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_CMD_BATT_MFG_ACCESS
/*
 * Enable support for battery hostcmd, supporting longer strings.
 * support for EC_CMD_BATTERY_GET_STATIC version 1.
 */
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2

/* Chipset config */
#define CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540
#define CONFIG_CHIPSET_X86_RSMRST_AFTER_S5

#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S4_RESIDENCY
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_LOW_POWER_IDLE

#define CONFIG_HOST_INTERFACE_ESPI_RESET_SLP_SX_VW_ON_ESPI_RST

#define CONFIG_BOARD_HAS_RTC_RESET
#undef CONFIG_S5_EXIT_WAIT
#define CONFIG_S5_EXIT_WAIT 10

#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_HOSTCMD_AP_RESET

/* ADL has new lower-power features that require extra-wide virtual wire
 * pulses. The EDS specifies 100 microseconds. */
#undef CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US
#define CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US 100

/* Buttons / Switches */
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_SWITCH

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_KEYPAD
#define CONFIG_KEYBOARD_PROTOCOL_8042

/* Thermal features */
#define CONFIG_THROTTLE_AP
#define CONFIG_CHIPSET_CAN_THROTTLE

#define CONFIG_PWM

/* Enable I2C Support */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

/* EDP back-light control defines */
#define CONFIG_BACKLIGHT_LID

/* UART COMMAND */
#define CONFIG_CMD_CHARGEN

/* USB Type C and USB PD defines */
/* Enable the new USB-C PD stack */
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_PD_REV30

#define CONFIG_CMD_HCDEBUG
#define CONFIG_CMD_PPC_DUMP
#define CONFIG_CMD_TCPC_DUMP

#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_ALT_MODE_UFP
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_NCT38XX

#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_HOSTCMD_PD_CONTROL /* Needed for TCPC FW update */
#define CONFIG_CMD_USB_PD_PE

/*
 * The PS8815 TCPC was found to require a 50ms delay to consistently work
 * with non-PD chargers.  Override the default low-power mode exit delay.
 */
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (50 * MSEC)

/* Enable USB3.2 DRD */
#define CONFIG_USB_PD_USB32_DRD

#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC

#define CONFIG_USBC_PPC
/* Note - SN5S330 support automatically adds
 * CONFIG_USBC_PPC_POLARITY
 * CONFIG_USBC_PPC_SBU
 * CONFIG_USBC_PPC_VCONN
 */
#define CONFIG_USBC_PPC_DEDICATED_INT

#define CONFIG_USBC_SS_MUX
#define CONFIG_USB_MUX_VIRTUAL

#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* Enabling SOP* communication */
#define CONFIG_CMD_USB_PD_CABLE
#define CONFIG_USB_PD_DECODE_SOP

/*
 * USB ID
 * This is allocated specifically for Brya
 * http://google3/hardware/standards/usb/
 */
#define CONFIG_USB_PID 0x504F
/* Device version of product. */
#define CONFIG_USB_BCD_DEV 0x0000

/*
 * These stack sizes were determined using "make analyzestack" for brya
 * and include about 15% headroom. Sizes are rounded to multiples of 64
 * bytes. Task stack sizes not listed here use more generic values (see
 * ec.tasklist).
 */
#define BASEBOARD_CHARGER_TASK_STACK_SIZE 1088
#define BASEBOARD_CHG_RAMP_TASK_STACK_SIZE 1088
#define BASEBOARD_CHIPSET_TASK_STACK_SIZE 1152
#define BASEBOARD_HOST_CMD_TASK_STACK_SIZE 928
#define BASEBOARD_PD_INT_TASK_STACK_SIZE 800
#define BASEBOARD_PD_TASK_STACK_SIZE 1216
#define BASEBOARD_POWERBTN_TASK_STACK_SIZE 1088
#define BASEBOARD_RGBKBD_TASK_STACK_SIZE 2048

#ifndef __ASSEMBLER__

#include "baseboard_usbc_config.h"
#include "cbi.h"
#include "common.h"
#include "extpower.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Check battery disconnect state.
 * This function will return if battery is initialized or not.
 * @return true - initialized. false - not.
 */
__override_proto bool board_battery_is_initialized(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
