/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Octopus board configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/*******************************************************************************
 * EC Config
 */

#define CONFIG_LTO

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))

/*
 * Variant EC defines. Pick one:
 * VARIANT_OCTOPUS_EC_NPCX796FB
 * VARIANT_OCTOPUS_EC_ITE8320
 */
#if defined(VARIANT_OCTOPUS_EC_NPCX796FB)
/* NPCX7 config */
#define NPCX_UART_MODULE2 1 /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2 0 /* [0:GPIO40/73, 1:GPIO93/A6] as TACH */
#define NPCX7_PWM1_SEL 0 /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
/* Flash is 1MB but reserve half for future use. */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)

#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* I2C Bus Configuration */
#define I2C_PORT_BATTERY NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC0 NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1 NPCX_I2C_PORT2_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT3_0
#define I2C_PORT_CHARGER NPCX_I2C_PORT4_1
#define I2C_PORT_SENSOR NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM_FLAGS 0x50

/* Enable PSL hibernate mode. */
#define CONFIG_HIBERNATE_PSL

/* EC variant determines USB-C variant */
#define VARIANT_OCTOPUS_USBC_STANDALONE_TCPCS

/* Allow the EC to enter deep sleep in S0 */
#define CONFIG_LOW_POWER_S0

/*
 * Increase period to prevent false positive hangs (b/255368431).
 * TODO(b/281584278): Reevaluate period when more data is available.
 */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 2100

#elif defined(VARIANT_OCTOPUS_EC_ITE8320)
/* IT83XX config */
#define CONFIG_IT83XX_VCC_1P8V
/* I2C Bus Configuration */
#define I2C_PORT_BATTERY IT83XX_I2C_CH_A /* Shared bus */
#define I2C_PORT_CHARGER IT83XX_I2C_CH_A /* Shared bus */
#define I2C_PORT_SENSOR IT83XX_I2C_CH_B
#define I2C_PORT_USBC0 IT83XX_I2C_CH_C
#define I2C_PORT_USBC1 IT83XX_I2C_CH_E
#define I2C_PORT_USB_MUX I2C_PORT_USBC0 /* For MUX driver */
#define I2C_PORT_EEPROM IT83XX_I2C_CH_F
#define I2C_ADDR_EEPROM_FLAGS 0x50
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT 2

/* EC variant determines USB-C variant */
#define VARIANT_OCTOPUS_USBC_ITE_EC_TCPCS

/*
 * Limit maximal ODR to 125Hz, the EC is using ~5ms per sample at
 * 48MHz core cpu clock.
 */
#define CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ 125000
#else
#error Must define a VARIANT_OCTOPUS_EC
#endif /* VARIANT_OCTOPUS_EC */

/* Common EC defines */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define CONFIG_I2C_BUS_MAY_BE_UNPOWERED
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define CONFIG_CRC8
#define CONFIG_CBI_EEPROM
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_DPTF
#define CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
#define CONFIG_BOARD_HAS_RTC_RESET
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_CMD_CHARGEN

/* Port80 -- allow larger buffer for port80 messages */
#undef CONFIG_PORT80_HISTORY_LEN
#define CONFIG_PORT80_HISTORY_LEN 256

/*
 * We don't need CONFIG_BACKLIGHT_LID since hardware AND's LID_OPEN and AP
 * signals with EC backlight enable signal.
 */

/*******************************************************************************
 * Battery/Charger/Power Config
 */

/*
 * Variant charger defines. Pick one:
 * VARIANT_OCTOPUS_CHARGER_ISL9238
 * VARIANT_OCTOPUS_CHARGER_BQ25703
 */
#if defined(VARIANT_OCTOPUS_CHARGER_ISL9238)
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
/*
 * ISL923x driver sets "Adapter insertion to Switching Debounce"
 * CONTROL2 REG 0x3DH <Bit 11> to 1 which is 150 ms
 */
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
/* Charger seems to overdraw by about 5% */
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 5
#elif defined(VARIANT_OCTOPUS_CHARGER_BQ25703)
#define CONFIG_CHARGER_BQ25703
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
/*
 * From BQ25703: CHRG_OK is HIGH after 50ms deglitch time.
 */
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 50
/* Charger seems to overdraw by about 5% */
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 5
#elif defined(CONFIG_CHARGER_RUNTIME_CONFIG)
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_BQ25710
#define CONFIG_CHARGER_SENSE_RESISTOR_AC_ISL9238 20
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR 10

#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
#else
#error Must define a VARIANT_OCTOPUS_CHARGER
#endif /* VARIANT_OCTOPUS_CHARGER */

/* Common charger defines */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER
/* Allow low-current USB charging */
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_USB_CHARGER

/* Common battery defines */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_DEVICE_CHEMISTRY "LION"
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_L
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART
#define CONFIG_HOSTCMD_BATTERY_V2

/*******************************************************************************
 * USB-C Configs
 * Automatically defined by VARIANT_OCTOPUS_EC_ variant.
 */

/*
 * Variant USBC defines. Pick one:
 * VARIANT_OCTOPUS_USBC_STANDALONE_TCPCS
 * VARIANT_OCTOPUS_USBC_ITE_EC_TCPCS (requires)
 */
#if defined(VARIANT_OCTOPUS_USBC_STANDALONE_TCPCS)
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#if !defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
#define CONFIG_USB_PD_TCPM_ANX7447 /* C0 TCPC: ANX7447QN */
#endif
#define CONFIG_USB_PD_TCPM_PS8751 /* C1 TCPC: PS8751 */
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USBC_PPC_NX20P3483
#elif defined(VARIANT_OCTOPUS_USBC_ITE_EC_TCPCS)
#undef CONFIG_USB_PD_TCPC_LOW_POWER
#undef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP /* C0 & C1 TCPC: ITE EC */
#define CONFIG_USB_MUX_IT5205 /* C0 MUX: IT5205 */
#define CONFIG_USB_PD_TCPM_PS8751 /* C1 Mux: PS8751 */
#define CONFIG_USB_PD_TCPM_PS8751_CUSTOM_MUX_DRIVER
#define CONFIG_USBC_PPC_SN5S330 /* C0 & C1 PPC: each SN5S330 */
#define CONFIG_USBC_PPC_VCONN
#define CONFIG_USBC_PPC_DEDICATED_INT
#else
#error Must define a VARIANT_OCTOPUS_USBC
#endif /* VARIANT_OCTOPUS_USBC */

/* Common USB-C defines */
#define USB_PD_PORT_TCPC_0 0
#define USB_PD_PORT_TCPC_1 1
#define CONFIG_USB_PID 0x5046

#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_PD_3A_PORTS 0
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_BC12_DETECT_MAX14637
#undef CONFIG_BC12_MAX14637_DELAY_FROM_OFF_TO_ON_MS
#define CONFIG_BC12_MAX14637_DELAY_FROM_OFF_TO_ON_MS 100
#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_CMD_PPC_DUMP

/* TODO(b/76218141): Use correct PD delay values */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* TODO(b/76218141): Use correct PD power values */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 45000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

/*******************************************************************************
 * USB-A Configs
 */

/* Common USB-A defines */
#define USB_PORT_COUNT 2
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USB_PORT_POWER_SMART_INVERTED
#define GPIO_USB1_ILIM_SEL GPIO_USB_A0_CHARGE_EN_L
#define GPIO_USB2_ILIM_SEL GPIO_USB_A1_CHARGE_EN_L

/*******************************************************************************
 * SoC / PCH Config
 */

/* Common SoC / PCH defines */
#define CONFIG_CHIPSET_GEMINILAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_HOST_INTERFACE_ESPI
/* TODO(b/74123961): Enable Virtual Wires after bringup */
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_PP5000_CONTROL
#define CONFIG_EXTPOWER_GPIO

/*******************************************************************************
 * Keyboard Config
 */

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD

#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_COL2_INVERTED
#undef CONFIG_KEYBOARD_VIVALDI

/*******************************************************************************
 * Sensor Config
 */

/* Common Sensor Defines */
#define CONFIG_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE
/*
 * Slew rate on the PP1800_SENSOR load switch requires a short delay on startup.
 */
#undef CONFIG_MOTION_SENSE_RESUME_DELAY_US
#define CONFIG_MOTION_SENSE_RESUME_DELAY_US (10 * MSEC)

#ifndef VARIANT_OCTOPUS_NO_SENSORS
/*
 * Interrupt and fifo are only used for base accelerometer
 * and the lid sensor is polled real-time (in forced mode).
 */
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* Power of 2 - Too large of a fifo causes too much timestamp jitter */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
#endif /* VARIANT_OCTOPUS_NO_SENSORS */

/* System safe mode for improved panic debugging */
#define CONFIG_SYSTEM_SAFE_MODE

/*
 * Sensor stack in EC/Kernel depends on a hardware interrupt pin from EC->AP, so
 * do not define CONFIG_MKBP_USE_HOST_EVENT since all octopus boards use
 * hardware pin to send interrupt from EC -> AP (except casta).
 */
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO

/* Free up more flash. */
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_MFALLOW
#undef CONFIG_CMD_MD
#undef CONFIG_CMD_MMAPINFO
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CONSOLE_HISTORY

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

/* Forward declare common (within octopus) board-specific functions */
void board_reset_pd_mcu(void);

#ifdef VARIANT_OCTOPUS_USBC_STANDALONE_TCPCS
void tcpc_alert_event(enum gpio_signal signal);
#endif

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
