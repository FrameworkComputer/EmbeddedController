/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

#include "compiler.h"
#include "stdbool.h"

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* NPCX7 config */
#define NPCX7_PWM1_SEL 0 /* GPIO C2 is not used as PWM1. */
#define NPCX_UART_MODULE2 1 /* GPIO64/65 are used as UART pins. */
/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */
#define CONFIG_I2C

/* Optional console commands */
#define CONFIG_CMD_CHARGER_DUMP

/* EC Defines */
#define CONFIG_ADC
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8
#define CONFIG_CBI_EEPROM
#define CONFIG_DPTF
#define CONFIG_HIBERNATE_PSL
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LTO
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Chipset config */
#define CONFIG_CHIPSET_COMETLAKE
#define CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_PP5000_CONTROL
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_KEYPAD
#define CONFIG_KEYBOARD_PROTOCOL_8042

/* Sensors */
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* Don't wake up from suspend on any MKBP event */
#define CONFIG_MKBP_EVENT_WAKEUP_MASK 0

/* I2C_PORT_ACCEL needs to be defined for i2c transactions */
#define I2C_PORT_ACCEL I2C_PORT_SENSOR

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#if !defined(BOARD_PALKIA)
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

/* Sensor console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#endif /* !BOARD_PALKIA */

/* Common charger defines */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ25710
#define CONFIG_CHARGER_DISCHARGE_ON_AC
/* Allow low-current USB charging */
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512
#undef CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR 10
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR_AC 10
/*
 * Don't allow the system to boot to S0 when the battery is low and unable to
 * communicate on locked systems (which haven't PD negotiated)
 */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT 15000
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15001

/* Common battery defines */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_DEVICE_CHEMISTRY "LION"
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART
#undef CONFIG_BATT_HOST_FULL_FACTOR
#define CONFIG_BATT_HOST_FULL_FACTOR 100

/* USB Type C and USB PD defines */
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#if defined(BOARD_PALKIA)
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#else
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#endif
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USBC_PPC_VCONN
#define CONFIG_USBC_SS_MUX
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USBC_PPC_DEDICATED_INT

#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_CMD_PPC_DUMP

/* Include CLI command needed to support CCD testing. */
#define CONFIG_CMD_CHARGEN

#define USB_PD_PORT_TCPC_0 0
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
#define USB_PD_PORT_TCPC_1 1
#endif

/* BC 1.2 */
#define CONFIG_USB_CHARGER

#if !defined(BOARD_PALKIA)
/* Common Sensor Defines */
#define CONFIG_TABLET_MODE
#endif

/* TODO(b/122273953): Use correct PD delay values */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* TODO(b/122273953): Use correct PD power values */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 60000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define I2C_PORT_SENSOR NPCX_I2C_PORT0_0
#define I2C_PORT_PPC0 NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1 NPCX_I2C_PORT2_0
#define I2C_PORT_TCPC0 NPCX_I2C_PORT3_0
#define I2C_PORT_THERMAL NPCX_I2C_PORT4_1
#define I2C_PORT_POWER NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM_FLAGS 0x50
#define I2C_PORT_BATTERY I2C_PORT_POWER
#define I2C_PORT_CHARGER I2C_PORT_POWER

/* Other common defines */
#define CONFIG_BACKLIGHT_LID
#define GPIO_ENABLE_BACKLIGHT GPIO_EDP_BKLTEN_OD

#define PP5000_PGOOD_POWER_SIGNAL_MASK POWER_SIGNAL_MASK(X86_PP5000_A_PGOOD)

#ifndef __ASSEMBLER__

enum mst_source {
	MST_TYPE_C0,
	MST_TYPE_C1,
	MST_HDMI,
};

/* Forward declare common (within Hatch) board-specific functions */
bool board_has_kb_backlight(void);
unsigned char get_board_sku(void);
unsigned char get_board_id(void);
void board_reset_pd_mcu(void);
void baseboard_mst_enable_control(enum mst_source, int level);
bool board_is_convertible(void);

FORWARD_DECLARE_ENUM(battery_present);

/* Check with variant about battery presence. */
enum battery_present variant_battery_present(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
