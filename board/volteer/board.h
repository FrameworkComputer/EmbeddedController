/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

#define CONFIG_POWER_BUTTON

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

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
#define ALS_COUNT		1
#define CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK \
	(BIT(LID_ACCEL) | BIT(CLEAR_ALS))

/* USB Type C and USB PD defines */
/*
 * USB-C port's USB2 & USB3 mapping from schematics
 * USB2 numbering on PCH - 1 to n
 * USB3 numbering on AP - 0 to n (PMC's USB3 numbering for MUX
 * configuration is - 1 to n hence add +1)
 */
#define USBC_PORT_0_USB2_NUM	9
#define USBC_PORT_0_USB3_NUM	1
#define USBC_PORT_1_USB2_NUM	4
#define USBC_PORT_1_USB3_NUM	2

/* Enabling Thunderbolt-compatible mode */
#define CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enabling USB4 mode */
#define CONFIG_USB_PD_USB4

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_AC_PRESENT			GPIO_ACOK_OD
#define GPIO_EC_INT_L			GPIO_EC_PCH_INT_ODL
#define GPIO_EN_PP5000			GPIO_EN_PP5000_A
#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW
#define GPIO_LID_OPEN			GPIO_EC_LID_OPEN
#define GPIO_KBD_KSO2			GPIO_EC_KSO_02_INV
#define GPIO_PCH_WAKE_L			GPIO_EC_PCH_WAKE_ODL
#define GPIO_PCH_PWRBTN_L		GPIO_EC_PCH_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L		GPIO_EC_PCH_RSMRST_ODL
#define GPIO_PCH_RTCRST			GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SLP_S0_L		GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L		GPIO_SLP_S3_L
#define GPIO_PG_EC_DSW_PWROK		GPIO_DSW_PWROK
#define GPIO_POWER_BUTTON_L		GPIO_H1_EC_PWR_BTN_ODL
#define GPIO_RSMRST_L_PGOOD		GPIO_PG_EC_RSMRST_ODL
#define GPIO_CPU_PROCHOT		GPIO_EC_PROCHOT_ODL
#define GPIO_SYS_RESET_L		GPIO_SYS_RST_ODL
#define GPIO_WP_L			GPIO_EC_WP_L
#define GPIO_USB_C1_BC12_INT_ODL	GPIO_USB_C1_MIX_INT_ODL
#define GPIO_VOLUME_UP_L		GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L		GPIO_EC_VOLDN_BTN_ODL
#define GMR_TABLET_MODE_GPIO_L		GPIO_TABLET_MODE_L

/* I2C Bus Configuration */
#define CONFIG_I2C
#define I2C_PORT_ACCEL		I2C_PORT_SENSOR
#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0		NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1		NPCX_I2C_PORT2_0
#define I2C_PORT_USB_1_MIX	NPCX_I2C_PORT3_0
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0

#define I2C_PORT_BATTERY	I2C_PORT_POWER
#define I2C_PORT_CHARGER	I2C_PORT_EEPROM

#define I2C_ADDR_EEPROM_FLAGS	0x50
#define CONFIG_I2C_MASTER


#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_LGC011,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel {
	PWM_CH_LED1_BLUE = 0,
	PWM_CH_LED2_GREEN,
	PWM_CH_LED3_RED,
	PWM_CH_LED4_SIDESEL,
	PWM_CH_FAN,
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT
};

enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	CLEAR_ALS,
	RGB_ALS,
	VSYNC,
	SENSOR_COUNT,
};

/* TODO: b/143375057 - Remove this code after power on. */
void c10_gate_change(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
