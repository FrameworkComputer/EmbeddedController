/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/*
 * The RAM and flash size combination on the NPCX797FC dose not leave
 * any unused flash space that can be used to store the .init_rom section.
 */
#undef CONFIG_CHIP_INIT_ROM_REGION

#define CONFIG_VBOOT_EFS2

#define CONFIG_POWER_BUTTON

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Chipset features */
#define CONFIG_POWER_PP5000_CONTROL

#undef NPCX_PWM1_SEL
#define NPCX_PWM1_SEL 0		/* GPIO C2 is not used as PWM1 */

/* LED defines */
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW	10

/* Keyboard features */
#define CONFIG_KEYBOARD_VIVALDI
#define CONFIG_KEYBOARD_REFRESH_ROW3

/* Sensors */
/* BMA253 accelerometer in base */
#define CONFIG_ACCEL_BMA255
#define CONFIG_ACCEL_KX022

/* BMI160 accel/gyro in base */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_ACCELGYRO_ICM426XX
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE		BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID		LID_ACCEL

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_PORT_MAX_COUNT			2

#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	30000 /* us */

/*
 * SN5S30 PPC supports up to 24V VBUS source and sink, however passive USB-C
 * cables only support up to 60W, the limitation of 45W is for the Collis
 * board.
 */
#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		45000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000

#undef CONFIG_USBC_RETIMER_INTEL_BB
#undef CONFIG_USBC_RETIMER_INTEL_BB_RUNTIME_CONFIG

/* USB Type A Features */
#define USB_PORT_COUNT			1
#define CONFIG_USB_PORT_POWER_DUMB

/* USBC PPC*/
#define CONFIG_USBC_PPC_SYV682X		/* USBC port C0/C1 */

#undef CONFIG_USB_MUX_RUNTIME_CONFIG
#undef CONFIG_USB_PD_TCPC_RUNTIME_CONFIG
#undef CONFIG_USB_PD_TCPM_RT1715
#undef CONFIG_USB_PD_TCPM_TUSB422

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */
#undef CONFIG_FANS

/* charger defines */
#define CONFIG_CHARGER_SENSE_RESISTOR		10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC	10

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
#define GPIO_PACKET_MODE_EN		GPIO_EC_H1_PACKET_MODE
#define GPIO_PCH_WAKE_L			GPIO_EC_PCH_WAKE_ODL
#define GPIO_PCH_PWRBTN_L		GPIO_EC_PCH_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L		GPIO_EC_PCH_RSMRST_ODL
#define GPIO_PCH_RTCRST			GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SLP_S0_L		GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L		GPIO_SLP_S3_L
#define GPIO_PCH_DSW_PWROK		GPIO_EC_PCH_DSW_PWROK
#define GPIO_PG_EC_DSW_PWROK		GPIO_DSW_PWROK
#define GPIO_POWER_BUTTON_L		GPIO_H1_EC_PWR_BTN_ODL
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
#define I2C_PORT_USB_0_MIX	NPCX_I2C_PORT3_0
#define I2C_PORT_USB_1_MIX	NPCX_I2C_PORT4_1
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0

#define I2C_PORT_BATTERY	I2C_PORT_POWER
#define I2C_PORT_CHARGER	I2C_PORT_EEPROM

#define I2C_ADDR_EEPROM_FLAGS	0x50
#define CONFIG_I2C_CONTROLLER


#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_C490,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_COUNT
};

enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

void board_reset_pd_mcu(void);
void motion_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
