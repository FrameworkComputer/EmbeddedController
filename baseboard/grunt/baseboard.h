/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Grunt family-specific configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

#if (defined(VARIANT_GRUNT_TCPC_0_ANX3429) + \
     defined(VARIANT_GRUNT_TCPC_0_ANX3447)) != 1
#error Must choose VARIANT_GRUNT_TCPC_0_ANX3429 or VARIANT_GRUNT_TCPC_0_ANX3447
#endif

/* NPCX7 config */
#define NPCX_UART_MODULE2 1 /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2 0 /* No tach. */
#define NPCX7_PWM1_SEL 0 /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
/* Flash is 1MB but reserve half for future use. */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/*
 * Increase watchdog period to prevent false positive hangs.
 */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 2100

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
#define CONFIG_HIBERNATE_PSL
#define CONFIG_HOST_INTERFACE_LPC
#define CONFIG_HOSTCMD_SKUID
#define CONFIG_I2C
#define CONFIG_I2C_BUS_MAY_BE_UNPOWERED
#define CONFIG_I2C_CONTROLLER
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LOW_POWER_S0
#define CONFIG_LTO
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR_NCP15WB
#define CONFIG_VBOOT_HASH
#define CONFIG_VOLUME_BUTTONS

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_L
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART

#define CONFIG_BC12_DETECT_MAX14637
#define CONFIG_CHARGER
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC

/*
 * This limit impairs compatibility with BC1.2 chargers that are not actually
 * capable of supplying 500 mA of current.  When the charger is paralleled with
 * the battery, raising this limit allows the power system to draw more current
 * from the charger during startup.  This improves compatibility with system
 * batteries that may become excessively imbalanced after extended periods of
 * rest.
 *
 * See also b/111214767
 */
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 5
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_USB_CHARGER

#define CONFIG_CHIPSET_STONEY
#define CONFIG_CHIPSET_RESET_HOOK
/*
 * ACOK from ISL9238 sometimes has a negative pulse after connecting
 * USB-C power. We want to ignore it. b/77455171
 */
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86

/*
 * On power-on, H1 releases the EC from reset but then quickly asserts and
 * releases the reset a second time. This means the EC sees 2 resets:
 * (1) power-on reset, (2) reset-pin reset. This config will
 * allow the second reset to be treated as a power-on.
 */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042

#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DP_HPD_GPIO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPC_LOW_POWER
#ifdef VARIANT_GRUNT_TCPC_0_ANX3429
#define CONFIG_USB_PD_TCPM_ANX3429
#elif defined(VARIANT_GRUNT_TCPC_0_ANX3447)
#define CONFIG_USB_PD_TCPM_ANX7447
#endif
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_PS8751
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USBC_PPC_DEDICATED_INT
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* USB-A config */
#define CONFIG_USB_PORT_POWER_DUMB
#define USB_PORT_COUNT 2

#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */

#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 45000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

/*
 * Require PD negotiation to be complete when we are in a low-battery condition
 * prior to releasing depthcharge to the kernel.
 */
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15001
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 3

/* Increase length of history buffer for port80 messages. */
#undef CONFIG_PORT80_HISTORY_LEN
#define CONFIG_PORT80_HISTORY_LEN 256

#define I2C_PORT_BATTERY I2C_PORT_POWER
#define I2C_PORT_CHARGER I2C_PORT_POWER
#define I2C_PORT_POWER NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC0 NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1 NPCX_I2C_PORT2_0
#define I2C_PORT_THERMAL_AP NPCX_I2C_PORT3_0
#define I2C_PORT_SENSOR NPCX_I2C_PORT7_0
/* Accelerometer and Gyroscope are the same device. */
#define I2C_PORT_ACCEL I2C_PORT_SENSOR

/* Sensors */
#define CONFIG_MKBP_EVENT
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* Thermal */
#define CONFIG_TEMP_SENSOR_SB_TSI

#ifndef VARIANT_GRUNT_NO_SENSORS
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is a power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs. */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
#endif /* VARIANT_GRUNT_NO_SENSORS */

#define USB_PD_PORT_ANX74XX 0
#define USB_PD_PORT_PS8751 1

/* System safe mode for improved panic debugging */
#define CONFIG_SYSTEM_SAFE_MODE

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_SOC,
	ADC_VBUS,
	ADC_SKU_ID1,
	ADC_SKU_ID2,
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

enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)

void board_reset_pd_mcu(void);

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void anx74xx_cable_det_interrupt(enum gpio_signal signal);

int board_get_version(void);
int board_is_convertible(void);
void board_update_sensor_config_from_sku(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
