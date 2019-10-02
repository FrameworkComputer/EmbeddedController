/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zork baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* No tach. */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q40 /* Internal SPI flash type. */

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
#define CONFIG_EC_FEATURE_BOARD_OVERRIDE
#define CONFIG_HIBERNATE_PSL
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_SKUID
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_LTO
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR_NCP15WB
#define CONFIG_VBOOT_HASH
#define CONFIG_VOLUME_BUTTONS

/* CBI EEPROM for board version and SKU ID */
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART

#define CONFIG_BC12_DETECT_PI3USB9201

#define CONFIG_CHARGER
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#define CONFIG_CHARGE_RAMP_HW

#define CONFIG_CHIPSET_STONEY
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_RESET_HOOK

#undef  CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86

#define CONFIG_FANS FAN_CH_COUNT
#undef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 50
#define CONFIG_THROTTLE_AP

#define CONFIG_LED_COMMON
#define CONFIG_CMD_LEDTEST
#define CONFIG_LED_ONOFF_STATES

/*
 * On power-on, H1 releases the EC from reset but then quickly asserts and
 * releases the reset a second time. This means the EC sees 2 resets:
 * (1) power-on reset, (2) reset-pin reset. If we add a delay between reset (1)
 * and configuring GPIO output levels, then reset (2) will happen before the
 * end of the delay so we avoid extra output toggles.
 */
#define CONFIG_GPIO_INIT_POWER_ON_DELAY_MS 100

#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_NCT38XX
#define CONFIG_IO_EXPANDER_PORT_COUNT USBC_PORT_COUNT

#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042

/* TODO(b/142284905): Enable new PD stack */
#if 0
/* Enable the new USB-C PD stack */
#define CONFIG_USB_PE_SM
#define CONFIG_USB_PRL_SM
#define CONFIG_USB_SM_FRAMEWORK
#define CONFIG_USB_TYPEC_SM
#define CONFIG_USB_TYPEC_DRP_ACC_TRYSRC
#define CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP
#endif

#define CONFIG_CMD_PD_CONTROL
#define CONFIG_USB_CHARGER
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_NCT38XX
#define CONFIG_USB_PD_TCPM_PS8751
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USBC_PPC
#define CONFIG_USBC_PPC_SBU
#define CONFIG_USBC_PPC_AOZ1380
#define CONFIG_USBC_PPC_NX20P3483
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_MUX_AMD_FP5

/* USB-A config */
#define USB_PORT_COUNT 2
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
#define CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE USB_CHARGE_MODE_CDP
#define CONFIG_USB_PORT_POWER_SMART_INVERTED
#define GPIO_USB1_ILIM_SEL IOEX_USB_A0_CHARGE_EN_L
#define GPIO_USB2_ILIM_SEL IOEX_USB_A1_CHARGE_EN_DB_L

#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	30000 /* us */
#define PD_VCONN_SWAP_DELAY		5000 /* us */

#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		45000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000

/*
 * Minimum conditions to start AP and perform swsync.  Note that when the
 * charger is connected via USB-PD analog signaling, the boot will proceed
 * regardless.
 */
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 3

/*
 * Require PD negotiation to be complete when we are in a low-battery condition
 * prior to releasing depthcharge to the kernel.
 */
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15001
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 3

/* Increase length of history buffer for port80 messages. */
#undef CONFIG_PORT80_HISTORY_LEN
#define CONFIG_PORT80_HISTORY_LEN 256

#define I2C_PORT_TCPC0		NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC1		NPCX_I2C_PORT1_0
#define I2C_PORT_BATTERY	NPCX_I2C_PORT2_0
#define I2C_PORT_CHARGER	NPCX_I2C_PORT2_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT2_0
#define I2C_PORT_USB_MUX	NPCX_I2C_PORT3_0
#define I2C_PORT_THERMAL	NPCX_I2C_PORT4_1
#define I2C_PORT_SENSOR		NPCX_I2C_PORT5_0
#define I2C_PORT_ACCEL		NPCX_I2C_PORT5_0
#define I2C_PORT_AP_AUDIO	NPCX_I2C_PORT6_1
#define I2C_PORT_AP_HDMI	NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS	0x50

/* Sensors */
#define CONFIG_MKBP_EVENT
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* Thermal */
#define CONFIG_TEMP_SENSOR_SB_TSI

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is a power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs. */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "math_util.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_SOC,
	ADC_CH_COUNT
};

enum power_signal {
	X86_SLP_S3_N,
	X86_SLP_S5_N,
	X86_S0_PGOOD,
	X86_S5_PGOOD,
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_CHARGER = 0,
	TEMP_SENSOR_SOC,
	TEMP_SENSOR_CPU,
	TEMP_SENSOR_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_FAN,
	PWM_CH_COUNT
};

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

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

/*
 * Matrix to rotate accelerators into the standard reference frame.  The default
 * is the identity which is correct for the reference design.  Variations of
 * Zork may need to change it for manufacturability.
 * For the lid:
 *  +x to the right
 *  +y up
 *  +z out of the page
 *
 * The principle axes of the body are aligned with the lid when the lid is in
 * the 180 degree position (open, flat).
 *
 * Boards within the Zork family may need to modify this definition at
 * board_init() time.
 */
extern mat33_fp_t zork_base_standard_ref;

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)

void board_reset_pd_mcu(void);

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);

int board_is_convertible(void);
void board_update_sensor_config_from_sku(void);

#ifdef CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP
int board_tcpc_fast_role_swap_enable(int port, int enable);
#endif

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
