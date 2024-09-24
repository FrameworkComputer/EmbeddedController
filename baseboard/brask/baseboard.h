/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brask baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/*
 * By default, enable all console messages excepted HC
 */
#define CC_DEFAULT (CC_ALL & ~(BIT(CC_HOSTCMD)))

/* NPCX9 config */
#define NPCX9_PWM1_SEL 1 /* GPIO C2 is used as PWM1. */
/*
 * This defines which pads (GPIO10/11 or GPIO64/65) are connected to
 * the "UART1" (NPCX_UART_PORT0) controller when used for
 * CONSOLE_UART.
 */
#define NPCX_UART_MODULE2 1 /* 1:GPIO64/65 for UART1 */

/* CrOS Board Info */
#define CONFIG_CBI_EEPROM
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8

/* EC Defines */
#define CONFIG_LTO
#define CONFIG_FPU

/* Verified boot configs */
#define CONFIG_VBOOT_EFS2
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Work around double CR50 reset by waiting in initial power on. */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

/* Host communication */
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S5
#define CONFIG_HOST_INTERFACE_ESPI_RESET_SLP_SX_VW_ON_ESPI_RST

/* LED */
#define CONFIG_LED_COMMON

/* Common charger defines */
#define CONFIG_CHARGE_MANAGER

/* USBC BC1.2 */
#define CONFIG_USB_CHARGER
#define CONFIG_BC12_DETECT_PI3USB9201

/* Support Barrel Jack */
#undef CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 45000

/* Chipset config */
#define CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S4_RESIDENCY
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_CHIPSET_X86_RSMRST_AFTER_S5

/*
 * TODO(b/191742284): When DAM enabled coreboot image is flashed on top of DAM
 * disabled coreboot, S5 exit is taking more than 4 seconds, then EC triggers
 * system shutdown. This WA deselects CONFIG_BOARD_HAS_RTC_RESET to prevent
 * EC from system shutdown.
 */
/* #define CONFIG_BOARD_HAS_RTC_RESET */

#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_HOSTCMD_AP_RESET

/* ADL has new low-power features that requires extra-wide virtual wire
 * pulses. The EDS specifies 100 microseconds. */
#undef CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US
#define CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US 100

/* Buttons */
#define CONFIG_DEDICATED_RECOVERY_BUTTON
#define CONFIG_EMULATED_SYSRQ
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_POWER_BUTTON_INIT_IDLE
#define CONFIG_POWER_BUTTON_X86
#undef CONFIG_BUTTON_DEBOUNCE
#define CONFIG_BUTTON_DEBOUNCE (100 * MSEC)

/* Matrix Keyboard Protocol */
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_INPUT_DEVICES
#define CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT

/* Thermal features */
#define CONFIG_DPTF
#define CONFIG_THROTTLE_AP

#define CONFIG_PWM

/* Enable I2C Support */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

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
#define CONFIG_USBC_PPC_POLARITY
#define CONFIG_USBC_PPC_SBU
#define CONFIG_USBC_PPC_VCONN
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
 * This is allocated specifically for Brask
 * http://google3/hardware/standards/usb/
 */
#define CONFIG_USB_PID 0x5058
/* Device version of product. */
#define CONFIG_USB_BCD_DEV 0x0000

/* Remove predefined features */
#undef CONFIG_HIBERNATE
#undef CONFIG_KEYBOARD_BOOT_KEYS
#undef CONFIG_KEYBOARD_RUNTIME_KEYS
#undef CONFIG_LID_SWITCH
#undef CONFIG_KEYBOARD_VIVALDI

#ifndef __ASSEMBLER__

#include "baseboard_usbc_config.h"
#include "common.h"
#include "extpower.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Configure run-time data structures and operation based on CBI data. This
 * typically includes customization for changes in the BOARD_VERSION and
 * FW_CONFIG fields in CBI. This routine is called from the baseboard after
 * the CBI data has been initialized.
 */
__override_proto void board_cbi_init(void);

/*
 * Initialize the FW_CONFIG from CBI data. If the CBI data is not valid, set the
 * FW_CONFIG to the board specific defaults.
 */
__override_proto void board_init_fw_config(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
