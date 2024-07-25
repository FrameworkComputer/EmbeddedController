/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zork baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

#if (defined(VARIANT_ZORK_TREMBYLE) + defined(VARIANT_ZORK_DALBOZ)) != 1
#error Must choose VARIANT_ZORK_TREMBYLE or VARIANT_ZORK_DALBOZ
#endif

/* NPCX7 config */
#define NPCX_UART_MODULE2 1 /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2 0 /* No tach. */
#define NPCX7_PWM1_SEL 0 /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q40 /* Internal SPI flash type. */

#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_HOSTCMD) | CC_MASK(CC_LPC)))

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

#define CONFIG_ADC
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BACKLIGHT_LID_ACTIVE_LOW
#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_HIBERNATE_PSL
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define CONFIG_I2C_UPDATE_IF_CHANGED
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR_NCP15WB
#define CONFIG_VBOOT_EFS2
#define CONFIG_VBOOT_HASH
#define CONFIG_VOLUME_BUTTONS

/* CBI EEPROM for board version and SKU ID */
#define CONFIG_CBI_EEPROM
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART
/*
 * Enable support for battery hostcmd, supporting longer strings.
 *
 * Vilboz battery options' model names vary in the 8th character, which is
 * truncated in the memory mapped battery info; differentiating them requires
 * support for EC_CMD_BATTERY_GET_STATIC version 1.
 */
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2

#define CONFIG_BC12_DETECT_PI3USB9201

#define CONFIG_CHARGER
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
/*
 * We would prefer to use CONFIG_CHARGE_RAMP_HW to enable legacy BC1.2 charging
 * but that feature of ISL9241 is broken (b/160287056) so we have to use
 * CONFIG_CHARGE_RAMP_SW instead.
 */
#define CONFIG_CHARGE_RAMP_SW

#define CONFIG_CHIPSET_STONEY
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_RESET_HOOK

#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_BUTTON_TO_PCH_CUSTOM
#define CONFIG_THROTTLE_AP

#ifdef VARIANT_ZORK_TREMBYLE
#define CONFIG_FANS FAN_CH_COUNT
#undef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 50
#endif

#define CONFIG_LED_COMMON
#define CONFIG_CMD_LEDTEST
#define CONFIG_LED_ONOFF_STATES

/*
 * On power-on, H1 releases the EC from reset but then quickly asserts and
 * releases the reset a second time. This means the EC sees 2 resets:
 * (1) power-on reset, (2) reset-pin reset. This config will
 * allow the second reset to be treated as a power-on.
 */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_NCT38XX

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042
#undef CONFIG_KEYBOARD_VIVALDI

/*
 * USB ID
 *
 * This is allocated specifically for Zork
 * http://google3/hardware/standards/usb/
 */
#define CONFIG_USB_PID 0x5040

#define CONFIG_USB_PD_REV30

/* Enable the TCPMv2 PD stack */
#define CONFIG_USB_PD_TCPMV2

#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USB_DRP_ACC_TRYSRC

/* Enable TCPMv2 Fast Role Swap */
/* Turn off until FRSwap is working */
#undef CONFIG_USB_PD_FRS_TCPC

#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_CMD_TCPC_DUMP
#define CONFIG_USB_CHARGER
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_DP_HPD_GPIO
#ifdef VARIANT_ZORK_TREMBYLE
/*
 * Use a custom HPD function that supports HPD on IO expander.
 * TODO(b/165622386) remove this when HPD is on EC GPIO.
 */
#define CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM
#endif
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_NCT38XX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USBC_PPC
#define CONFIG_USBC_PPC_SBU
#define CONFIG_USBC_PPC_AOZ1380
#define CONFIG_USBC_RETIMER_PI3HDX1204
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_MUX_AMD_FP5

#if defined(VARIANT_ZORK_TREMBYLE)
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USBC_PPC_NX20P3483
#define CONFIG_USBC_RETIMER_PS8802
#define CONFIG_USBC_RETIMER_PS8818
#define CONFIG_IO_EXPANDER_PORT_COUNT USBC_PORT_COUNT
#define CONFIG_USB_MUX_RUNTIME_CONFIG
/* USB-A config */
#define GPIO_USB1_ILIM_SEL IOEX_USB_A0_CHARGE_EN_L
#define GPIO_USB2_ILIM_SEL IOEX_USB_A1_CHARGE_EN_DB_L
/* PS8818 RX Input Termination - default value */
#define ZORK_PS8818_RX_INPUT_TERM PS8818_RX_INPUT_TERM_112_OHM
#elif defined(VARIANT_ZORK_DALBOZ)
#define CONFIG_IO_EXPANDER_PORT_COUNT IOEX_PORT_COUNT
#endif

/* USB-A config */
#define USB_PORT_COUNT USBA_PORT_COUNT
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USB_PORT_POWER_SMART_INVERTED

#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */

#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 65000
#define PD_MAX_CURRENT_MA 3250
#define PD_MAX_VOLTAGE_MV 20000

/* Round up 3250 max current to multiple of 128mA for ISL9241 AC prochot. */
#define ZORK_AC_PROCHOT_CURRENT_MA 3328

/*
 * EC will boot AP to depthcharge if: (BAT >= 4%) || (AC >= 50W)
 * CONFIG_CHARGER_LIMIT_* is not set, so there is no additional restriction on
 * Depthcharge to boot OS.
 */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 50000

/* Increase length of history buffer for port80 messages. */
#undef CONFIG_PORT80_HISTORY_LEN
#define CONFIG_PORT80_HISTORY_LEN 256

/* Increase console output buffer since we have the RAM available. */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#define I2C_PORT_TCPC0 NPCX_I2C_PORT0_0
#define I2C_PORT_USBA0 NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC1 NPCX_I2C_PORT1_0
#define I2C_PORT_USBA1 NPCX_I2C_PORT1_0
#define I2C_PORT_USB_AP_MUX NPCX_I2C_PORT3_0
#define I2C_PORT_THERMAL_AP NPCX_I2C_PORT4_1
#define I2C_PORT_SENSOR NPCX_I2C_PORT5_0
#define I2C_PORT_ACCEL I2C_PORT_SENSOR
#define I2C_PORT_EEPROM I2C_PORT_SENSOR
#define I2C_PORT_AP_AUDIO NPCX_I2C_PORT6_1

#if defined(VARIANT_ZORK_TREMBYLE)
#define CONFIG_CHARGER_RUNTIME_CONFIG
#define I2C_PORT_BATTERY NPCX_I2C_PORT2_0
#define I2C_PORT_CHARGER_V0 NPCX_I2C_PORT2_0
#define I2C_PORT_CHARGER_V1 NPCX_I2C_PORT4_1
#define I2C_PORT_AP_HDMI NPCX_I2C_PORT7_0
#elif defined(VARIANT_ZORK_DALBOZ)
#define I2C_PORT_BATTERY_V0 NPCX_I2C_PORT2_0
#define I2C_PORT_BATTERY_V1 NPCX_I2C_PORT7_0
#define I2C_PORT_CHARGER NPCX_I2C_PORT2_0
#endif

#define I2C_ADDR_EEPROM_FLAGS 0x50

#define CONFIG_MKBP_EVENT
/* Host event is required to wake from sleep */
#define CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT
/* Required to enable runtime configuration */
#define CONFIG_MKBP_EVENT_WAKEUP_MASK (BIT(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED))

/* Sensors */
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* Thermal */
#define CONFIG_TEMP_SENSOR_SB_TSI

#ifdef HAS_TASK_MOTIONSENSE
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is a power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs. */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
#endif

/* Audio */
#define CONFIG_AUDIO_CODEC
#define CONFIG_AUDIO_CODEC_DMIC
#define CONFIG_AUDIO_CODEC_I2S_RX

/* CLI COMMAND */
#define CONFIG_CMD_CHARGEN

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum power_signal {
	X86_SLP_S3_N,
	X86_SLP_S5_N,
	X86_S0_PGOOD,
	X86_S5_PGOOD,
	POWER_SIGNAL_COUNT
};

enum fan_channel {
	FAN_CH_0 = 0,
	/* Number of FAN channels */
	FAN_CH_COUNT,
};

#ifdef VARIANT_ZORK_TREMBYLE
enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };
#endif

enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

extern const struct thermistor_info thermistor_info;

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)

void mst_hpd_interrupt(enum ioex_signal signal);
void sbu_fault_interrupt(enum ioex_signal signal);

#ifdef VARIANT_ZORK_TREMBYLE
void board_reset_pd_mcu(void);

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
__override_proto void ppc_interrupt(enum gpio_signal signal);
#endif

void board_print_temps(void);

/* GPIO or IOEX signal used to set IN_HPD on DB retimer. */
extern int board_usbc1_retimer_inhpd;

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
