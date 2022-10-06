/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Optional features */
#undef NPCX7_PWM1_SEL
#define NPCX7_PWM1_SEL 0 /* GPIO C2 is not used as PWM1. */

/*
 * The RAM and flash size combination on the the NPCX797FC does not leave
 * any unused flash space that can be used to store the .init_rom section.
 */
#undef CONFIG_CHIP_INIT_ROM_REGION

#define CONFIG_VBOOT_EFS2

#define CONFIG_POWER_BUTTON

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Chipset features */
#define CONFIG_POWER_PP5000_CONTROL
#define CONFIG_CPU_PROCHOT_GATE_ON_C10

/* LED defines */
#define CONFIG_LED_ONOFF_STATES

/* Keyboard features */
#define CONFIG_KEYBOARD_FACTORY_TEST
#define CONFIG_KEYBOARD_REFRESH_ROW3

/* Keyboard backliht */
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT

/* Sensors */
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
/* BMI160 Base accel/gyro */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_ICM426XX /* Base accel second source*/
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* Lid operates in forced mode, base in FIFO */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

/* BMA253 Lid accel */
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCEL_BMA255
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2

#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */

/*
 * SN5S30 PPC supports up to 24V VBUS source and sink, however passive USB-C
 * cables only support up to 60W.
 */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 60000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

#define CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY
#ifdef BOARD_VOXEL_ECMODEENTRY
#undef CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY
#endif

/* Enabling Thunderbolt-compatible mode */
#define CONFIG_USB_PD_TBT_COMPAT_MODE

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

/* USBC PPC*/
#define CONFIG_USBC_PPC_SYV682X /* USBC port C0/C1 */
#define CONFIG_USB_PD_FRS_PPC
#undef CONFIG_USB_PD_TCPC_RUNTIME_CONFIG
#undef CONFIG_USB_PD_TCPM_TUSB422
#undef CONFIG_USB_MUX_RUNTIME_CONFIG

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */

/* charger defines */
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10

/* Retimer */
#undef CONFIG_USBC_RETIMER_INTEL_BB
#undef CONFIG_USBC_RETIMER_INTEL_BB_RUNTIME_CONFIG

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_AC_PRESENT GPIO_ACOK_OD
#define GPIO_EC_INT_L GPIO_EC_PCH_INT_ODL
#define GPIO_EN_PP5000 GPIO_EN_PP5000_A
#define GPIO_ENTERING_RW GPIO_EC_ENTERING_RW
#define GPIO_LID_OPEN GPIO_EC_LID_OPEN
#define GPIO_KBD_KSO2 GPIO_EC_KSO_02_INV
#define GPIO_PACKET_MODE_EN GPIO_EC_H1_PACKET_MODE
#define GPIO_PCH_WAKE_L GPIO_EC_PCH_WAKE_ODL
#define GPIO_PCH_PWRBTN_L GPIO_EC_PCH_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L GPIO_EC_PCH_RSMRST_ODL
#define GPIO_PCH_RTCRST GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SLP_S0_L GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L GPIO_SLP_S3_L
#define GPIO_PCH_DSW_PWROK GPIO_EC_PCH_DSW_PWROK
#define GPIO_POWER_BUTTON_L GPIO_H1_EC_PWR_BTN_ODL
#define GPIO_CPU_PROCHOT GPIO_EC_PROCHOT_ODL
#define GPIO_SYS_RESET_L GPIO_SYS_RST_ODL
#define GPIO_WP_L GPIO_EC_WP_L
#define GPIO_USB_C1_BC12_INT_ODL GPIO_USB_C1_MIX_INT_ODL
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

/* I2C Bus Configuration */
#define CONFIG_I2C
#define I2C_PORT_ACCEL I2C_PORT_SENSOR
#define I2C_PORT_SENSOR NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0 NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1 NPCX_I2C_PORT2_0
#define I2C_PORT_POWER NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0

#define I2C_PORT_BATTERY I2C_PORT_POWER
#define I2C_PORT_CHARGER I2C_PORT_EEPROM

#define I2C_ADDR_EEPROM_FLAGS 0x50
#define CONFIG_I2C_CONTROLLER

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_AP19B8M,
	BATTERY_LGC_AP18C8K,
	BATTERY_COSMX_AP20CBL,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel { PWM_CH_FAN, PWM_CH_KBLIGHT, PWM_CH_COUNT };

enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

void board_reset_pd_mcu(void);

void motion_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
