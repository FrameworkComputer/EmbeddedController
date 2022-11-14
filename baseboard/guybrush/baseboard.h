/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* NPCX9 config */
#define CONFIG_PORT80_4_BYTE
#define NPCX9_PWM1_SEL 1 /* GPIO C2 is used as PWM1. */
#define NPCX_UART_MODULE2 1 /* GPIO64/65 are used as UART pins. */

/* Optional features */
#define CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT
#define CONFIG_LTO /* Link-Time Optimizations to reduce code size */
#define CONFIG_I2C_DEBUG /* Print i2c traces */
#define CONFIG_CMD_S5_TIMEOUT /* Allow a user-specified timeout to exit S5 */

/* Detect sleep hang after 10s */
#undef CONFIG_SLEEP_TIMEOUT_MS
#define CONFIG_SLEEP_TIMEOUT_MS 10000

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Vboot Config */
#define CONFIG_CRC8
#define CONFIG_VBOOT_EFS2
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define GPIO_ENTERING_RW GPIO_EC_ENTERING_RW
#define GPIO_PACKET_MODE_EN GPIO_EC_GSC_PACKET_MODE

/* CBI Config */
#define CONFIG_CBI_EEPROM
#define CONFIG_BOARD_VERSION_CBI

/* Power Config */
#define CONFIG_CHIPSET_X86_RSMRST_DELAY
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HIBERNATE_PSL
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_TO_PCH_CUSTOM
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define G3_TO_PWRBTN_DELAY_MS 16
#define GPIO_AC_PRESENT GPIO_ACOK_OD
#define GPIO_EN_PWR_A GPIO_EN_PWR_S5
#define GPIO_PCH_PWRBTN_L GPIO_EC_SOC_PWR_BTN_L
#define GPIO_PCH_RSMRST_L GPIO_EC_SOC_RSMRST_L
#define GPIO_PCH_SLP_S0_L GPIO_SLP_S3_S0I3_L
#define GPIO_PCH_SLP_S3_L GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S5_L GPIO_SLP_S5_L
#define GPIO_PCH_SYS_PWROK GPIO_EC_SOC_PWR_GOOD
#define GPIO_PCH_WAKE_L GPIO_EC_SOC_WAKE_L
#define GPIO_POWER_BUTTON_L GPIO_MECH_PWR_BTN_ODL
#define GPIO_S0_PGOOD GPIO_PG_PCORE_S0_R_OD
#define GPIO_S5_PGOOD GPIO_PG_PWR_S5
#define GPIO_SYS_RESET_L GPIO_EC_SYS_RST_L
#define SAFE_RESET_VBUS_DELAY_MS 900
#define SAFE_RESET_VBUS_MV 5000
/*
 * On power-on, H1 releases the EC from reset but then quickly asserts and
 * releases the reset a second time. This means the EC sees 2 resets:
 * (1) power-on reset, (2) reset-pin reset. This config will
 * allow the second reset to be treated as a power-on.
 */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

/* Thermal Config */
#define CONFIG_ADC
#define CONFIG_AMD_SB_RMI
#define CONFIG_AMD_STT
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B
#define CONFIG_THROTTLE_AP
#define CONFIG_TEMP_SENSOR_SB_TSI
#define CONFIG_THERMISTOR
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define GPIO_CPU_PROCHOT GPIO_PROCHOT_ODL

/* Flash Config */
/* See config_chip-npcx9.h for SPI flash configuration */
#undef CONFIG_SPI_FLASH /* Don't enable external flash interface */
#define GPIO_WP_L GPIO_EC_WP_L

/* Host communication */
#define CONFIG_CMD_APTHROTTLE
#define CONFIG_CMD_CHARGEN
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT
#define GPIO_EC_INT_L GPIO_EC_SOC_INT_L

/* Chipset config */
#define CONFIG_CHIPSET_CEZANNE
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_RESET_HOOK

/* Keyboard Config */
#define CONFIG_MKBP_INPUT_DEVICES
#define CONFIG_KEYBOARD_BACKLIGHT

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_VIVALDI
#define CONFIG_KBLIGHT_ENABLE_PIN
#define GPIO_EN_KEYBOARD_BACKLIGHT GPIO_EN_KB_BL
#define GPIO_KBD_KSO2 GPIO_EC_KSO_02_INV

/* Sensors */
#ifdef HAS_TASK_MOTIONSENSE

#define CONFIG_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE
#define GPIO_TABLET_MODE_L GPIO_TABLET_MODE
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is a power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs. */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)
#endif /* HAS_TASK_MOTIONSENSE */

/* Backlight config */
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BACKLIGHT_LID_ACTIVE_LOW
#define GPIO_ENABLE_BACKLIGHT_L GPIO_EC_DISABLE_DISP_BL

/* Battery Config */
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_ODL
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2
#define CONFIG_BC12_DETECT_PI3USB9201
/* Enable PEC to detect potential corruption on read (b/230520109) */
#define CONFIG_SMBUS_PEC

/* Charger Config */
#define CONFIG_CHARGER
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20

/*
 * EC will boot AP to depthcharge if: (BAT >= 2%) || (AC >= 65W)
 * CONFIG_CHARGER_LIMIT_* is not set, so there is no additional restriction on
 * Depthcharge to boot OS.
 */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 65000

/*
 * We would prefer to use CONFIG_CHARGE_RAMP_HW to enable legacy BC1.2 charging
 * but that feature of ISL9241 is broken (b/160287056) so we have to use
 * CONFIG_CHARGE_RAMP_SW instead.
 */
#define CONFIG_CHARGE_RAMP_SW

/* USB Type C and USB PD config */
#define CONFIG_USB_PD_REV30
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_CMD_TCPC_DUMP
#define CONFIG_USB_CHARGER
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_5V_EN_CUSTOM
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_DP_HPD_GPIO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_FRS_TCPC
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_NCT38XX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USBC_PPC
#define CONFIG_USBC_PPC_SBU
#define CONFIG_USBC_PPC_AOZ1380
#define CONFIG_USBC_RETIMER_PI3HDX1204
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USBC_PPC_NX20P3483
#define CONFIG_USBC_RETIMER_PS8811
#define CONFIG_USBC_RETIMER_PS8818
#define CONFIG_USB_MUX_RUNTIME_CONFIG
#define CONFIG_USB_MUX_AMD_FP6

#define GPIO_USB_C0_DP_HPD GPIO_USB_C0_HPD
#define GPIO_USB_C1_DP_HPD GPIO_USB_C1_HPD

#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_NCT38XX
#define CONFIG_IO_EXPANDER_PORT_COUNT USBC_PORT_COUNT

#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */

/* USB-A config */
#define USB_PORT_COUNT USBA_PORT_COUNT
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USB_PORT_POWER_SMART_INVERTED

#define GPIO_USB1_ILIM_SEL IOEX_USB_A0_LIMIT_SDP
#define GPIO_USB2_ILIM_SEL IOEX_USB_A1_LIMIT_SDP_DB

/* Round up 5000 max current to multiple of 128mA for ISL9241 AC prochot. */
#define GUYBRUSH_AC_PROCHOT_CURRENT_MA 5120

/*
 * USB ID - This is allocated specifically for Guybrush
 */
#define CONFIG_USB_PID 0x504D

/* BC 1.2 */
/*
 * For legacy BC1.2 charging with CONFIG_CHARGE_RAMP_SW, ramp up input current
 * until voltage drops to 4.5V. Don't go lower than this to be kind to the
 * charger (see b/67964166).
 */
#define BC12_MIN_VOLTAGE 4500

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_BUS_MAY_BE_UNPOWERED
#define CONFIG_I2C_CONTROLLER
#define CONFIG_I2C_UPDATE_IF_CHANGED
#define I2C_PORT_TCPC0 NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC1 NPCX_I2C_PORT1_0
#define I2C_PORT_BATTERY NPCX_I2C_PORT2_0
#define I2C_PORT_USB_MUX NPCX_I2C_PORT3_0
#define I2C_PORT_POWER NPCX_I2C_PORT4_1
#define I2C_PORT_CHARGER I2C_PORT_POWER
#define I2C_PORT_EEPROM NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR NPCX_I2C_PORT6_1
#define I2C_PORT_THERMAL_AP NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM_FLAGS 0x50

/* Volume Button Config */
#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_VOLDN_BTN_ODL

/* Fan Config */
#define CONFIG_FANS FAN_CH_COUNT

/* LED Config */
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Power input signals */
enum power_signal {
	X86_SLP_S0_N, /* SOC  -> SLP_S3_S0I3_L */
	X86_SLP_S3_N, /* SOC  -> SLP_S3_L */
	X86_SLP_S5_N, /* SOC  -> SLP_S5_L */

	X86_S0_PGOOD, /* PMIC -> S0_PWROK_OD */
	X86_S5_PGOOD, /* PMIC -> S5_PWROK */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT,
};

/* USB-C ports */
enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

/* USB-A ports */
enum usba_port { USBA_PORT_A0 = 0, USBA_PORT_A1, USBA_PORT_COUNT };

/* TMP112 sensors */
enum tmp112_sensor {
	TMP112_SOC,
	TMP112_AMB,
	TMP112_COUNT,
};

enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

/* PWM Channels */
enum pwm_channel {
	PWM_CH_FAN = 0,
	PWM_CH_KBLIGHT,
	PWM_CH_LED_CHRG,
	PWM_CH_LED_FULL,
	PWM_CH_COUNT
};

/* Fan Channels */
enum fan_channel {
	FAN_CH_0 = 0,
	/* Number of FAN channels */
	FAN_CH_COUNT,
};
enum mft_channel {
	MFT_CH_0 = 0,
	/* Number of MFT channels */
	MFT_CH_COUNT,
};

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void sbu_fault_interrupt(enum ioex_signal signal);

void baseboard_en_pwr_pcore_signal(enum gpio_signal signal);
void baseboard_en_pwr_s0(enum gpio_signal signal);

int board_get_soc_temp_k(int idx, int *temp_k);

/* CBI utility functions */
uint32_t get_sku_id(void);
uint32_t get_board_version(void);
uint32_t get_fw_config(void);
/* Board callback after CBI has been initialized */
__overridable void board_cbi_init(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
