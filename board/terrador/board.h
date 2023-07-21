/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Free flash space */
#define CONFIG_USB_PD_DEBUG_LEVEL 0
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CMD_BATTFAKE
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_I2C_SCAN
#define CONFIG_DEBUG_ASSERT_BRIEF
#undef CONFIG_CONSOLE_HISTORY
#undef CONFIG_ACCEL_SPOOF_MODE
#undef CONFIG_USB_PD_TCPMV1_DEBUG
#undef CONFIG_CMD_SYSLOCK
#undef CONFIG_CMD_APTHROTTLE
#undef CONFIG_CMD_TIMERINFO
#undef CONFIG_CMD_MD
#undef CONFIG_CMD_I2C_SCAN
#undef CONFIG_CMD_I2C_XFER
#undef CONFIG_CMD_AP_RESET_LOG
#undef CONFIG_CMD_ADC
#undef CONFIG_CMD_CHARGE_SUPPLIER_INFO
#undef CONFIG_CMD_CHARGER_DUMP
#undef CONFIG_CMD_IDLE_STATS
#undef CONFIG_CMD_MFALLOW
#undef CONFIG_CMD_MMAPINFO
#undef CONFIG_CMD_SLEEPMASK
#undef CONFIG_CMD_SLEEPMASK_SET
#undef CONFIG_CMD_USB_PD_CABLE
#undef CONFIG_CMD_FLASHINFO
#undef CONFIG_CMD_FLASH_WP
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_CMD_POWER_AVG
#undef CONFIG_CMD_REGULATOR
#undef CONFIG_POWER_SLEEP_FAILURE_DETECTION

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

#define CONFIG_VBOOT_EFS2

#define CONFIG_POWER_BUTTON

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Chipset features */
#define CONFIG_POWER_PP5000_CONTROL

/* LED defines */
#define CONFIG_LED_PWM
/* Although there are 2 LEDs, they are both controlled by the same lines. */
#define CONFIG_LED_PWM_COUNT 1

/* Keyboard features */

/* Sensors */
/* BMA253 accelerometer in base */
#define CONFIG_ACCEL_BMA255

/* BMI260 accel/gyro in base */
#define CONFIG_ACCELGYRO_BMI260
#define CONFIG_ACCELGYRO_BMI260_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* TCS3400 ALS */
#define CONFIG_ALS
#define ALS_COUNT 1
#define CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (BIT(LID_ACCEL) | BIT(CLEAR_ALS))

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

/* Enabling Thunderbolt-compatible mode */
#define CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enabling USB4 mode */
#define CONFIG_USB_PD_USB4
#define USBC_PORT_C0_BB_RETIMER_I2C_ADDR 0x40
#define USBC_PORT_C1_BB_RETIMER_I2C_ADDR 0x40

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

/* USBC PPC*/
#define CONFIG_USBC_PPC_SYV682X /* USBC port C0/C1 */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */
#undef CONFIG_FANS

/* charger defines */
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10

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
#define GPIO_PG_EC_DSW_PWROK GPIO_DSW_PWROK
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
#define I2C_PORT_USB_0_MIX NPCX_I2C_PORT3_0
#define I2C_PORT_USB_1_MIX NPCX_I2C_PORT4_1
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
	BATTERY_LGC011,
	BATTERY_LGC_AP18C8K,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel {
	PWM_CH_LED1_BLUE = 0,
	PWM_CH_LED2_RED,
	PWM_CH_LED3_GREEN,
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT
};

enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	CLEAR_ALS,
	RGB_ALS,
	SENSOR_COUNT,
};

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
