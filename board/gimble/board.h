/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "compile_time_macros.h"

/* Baseboard features */
#include "baseboard.h"

/*
 * This will happen automatically on NPCX9 ES2 and later. Do not remove
 * until we can confirm all earlier chips are out of service.
 */
#define CONFIG_HIBERNATE_PSL_VCC1_RST_WAKEUP

#define CONFIG_MP2964

/* Sensors */
/* BMA253 accelerometer in lid */
#define CONFIG_ACCEL_BMA255

#define CONFIG_ACCELGYRO_BMI160 /* Base accel/gyro */
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* BMA422 accelerometer in lid */
#define CONFIG_ACCEL_BMA4XX

#define CONFIG_ACCELGYRO_LSM6DSM /* Base accel/gyro */
#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

/* Lid accel */
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

/* Sensor console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

/* USB Type C and USB PD defines */
#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_NCT38XX
#define CONFIG_IO_EXPANDER_PORT_COUNT 1

#define CONFIG_USB_PD_TCPM_PS8815
#define CONFIG_USB_PD_TCPM_PS8815_FORCE_DID
#undef CONFIG_USBC_RETIMER_INTEL_BB

#define CONFIG_USBC_PPC_SYV682X
#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50
#define CONFIG_USBC_PPC_NX20P3483

#define CONFIG_USB_PD_FRS
#define CONFIG_USB_PD_FRS_PPC

/* measure and check these values on gimble */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* I2C speed console command */
#define CONFIG_CMD_I2C_SPEED

/* I2C control host command */
#define CONFIG_HOSTCMD_I2C_CONTROL

/*
 * Passive USB-C cables only support up to 60W.
 */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 60000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_AC_PRESENT GPIO_ACOK_OD
#define GPIO_CPU_PROCHOT GPIO_EC_PROCHOT_ODL
#define GPIO_EC_INT_L GPIO_EC_PCH_INT_ODL
#define GPIO_ENABLE_BACKLIGHT GPIO_EC_EN_EDP_BL
#define GPIO_ENTERING_RW GPIO_EC_ENTERING_RW
#define GPIO_KBD_KSO2 GPIO_EC_KSO_02_INV
#define GPIO_PACKET_MODE_EN GPIO_EC_GSC_PACKET_MODE
#define GPIO_PCH_PWRBTN_L GPIO_EC_PCH_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L GPIO_EC_PCH_RSMRST_L
#define GPIO_PCH_RTCRST GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SLP_S0_L GPIO_SYS_SLP_S0IX_L
#define GPIO_PCH_SLP_S3_L GPIO_SLP_S3_L
#define GPIO_TEMP_SENSOR_POWER GPIO_SEQ_EC_DSW_PWROK

/*
 * GPIO_EC_PCH_INT_ODL is used for MKBP events as well as a PCH wakeup
 * signal.
 */
#define GPIO_PCH_WAKE_L GPIO_EC_PCH_INT_ODL
#define GPIO_PG_EC_ALL_SYS_PWRGD GPIO_SEQ_EC_ALL_SYS_PG
#define GPIO_PG_EC_DSW_PWROK GPIO_SEQ_EC_DSW_PWROK
#define GPIO_PG_EC_RSMRST_ODL GPIO_SEQ_EC_RSMRST_ODL
#define GPIO_POWER_BUTTON_L GPIO_GSC_EC_PWR_BTN_ODL
#define GPIO_SYS_RESET_L GPIO_SYS_RST_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_WP_L GPIO_EC_WP_ODL

/* System has back-lit keyboard */
#define CONFIG_PWM_KBLIGHT

/* Keyboard features */
#define CONFIG_KEYBOARD_VIVALDI
#define CONFIG_KEYBOARD_REFRESH_ROW3

/* I2C Bus Configuration */

#define I2C_PORT_SENSOR NPCX_I2C_PORT0_0

#define I2C_PORT_USB_C0_TCPC NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1_TCPC NPCX_I2C_PORT4_1

#define I2C_PORT_USB_C0_PPC NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_PPC NPCX_I2C_PORT4_1

#define I2C_PORT_USB_C0_BC12 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_BC12 NPCX_I2C_PORT4_1

#define I2C_PORT_BATTERY NPCX_I2C_PORT5_0
#define I2C_PORT_CHARGER NPCX_I2C_PORT7_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_PORT_MP2964 NPCX_I2C_PORT7_0

/* define this to aviod error on CONFIG_ACCELGYRO_BMI_COMM_I2C */
#define I2C_PORT_ACCEL I2C_PORT_SENSOR

#define I2C_ADDR_EEPROM_FLAGS 0x50

#define I2C_ADDR_MP2964_FLAGS 0x20

/* Disabling Thunderbolt-compatible mode */
#undef CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enabling USB4 mode */
#undef CONFIG_USB_PD_USB4

/* Retimer */
#undef CONFIG_USBC_RETIMER_FW_UPDATE

/* Thermal features */
#define CONFIG_FANS FAN_CH_COUNT
#define CONFIG_THERMISTOR
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

/* LED defines */
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/*
 * TODO(b/181271666): no fan control loop until sensors are tuned
 */
/* Fan features */
#define CONFIG_CUSTOM_FAN_CONTROL

/* Charger defines */
#define CONFIG_CHARGER_BQ25720
#define CONFIG_CHARGER_BQ25720_VSYS_TH2_CUSTOM
#define CONFIG_CHARGER_BQ25720_VSYS_TH2_DV 70
#define CONFIG_CHARGE_RAMP_SW
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR 10
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_BQ25710_PSYS_SENSING

/* PROCHOT defines */
#define BATT_MAX_CONTINUE_DISCHARGE_WATT 45

/* Prochot assertion/deassertion ratios*/
#define PROCHOT_ADAPTER_WATT_RATIO 97
#define PROCHOT_ASSERTION_BATTERY_RATIO 95
#define PROCHOT_DEASSERTION_BATTERY_RATIO 85
#define PROCHOT_ASSERTION_PD_RATIO 104
#define PROCHOT_DEASSERTION_PD_RATIO 94
#define PROCHOT_DEASSERTION_PD_BATTERY_RATIO 95
#define PROCHOT_ASSERTION_ADAPTER_RATIO 102
#define PROCHOT_DEASSERTION_ADAPTER_RATIO 100
#define PROCHOT_DEASSERTION_ADAPTER_BATT_RATIO 90

#ifndef __ASSEMBLER__

#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"
#include "usbc_config.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1_DDR_SOC,
	ADC_TEMP_SENSOR_2_FAN,
	ADC_TEMP_SENSOR_3_CHARGER,
	ADC_IADPT,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1_DDR_SOC,
	TEMP_SENSOR_2_FAN,
	TEMP_SENSOR_3_CHARGER,
	TEMP_SENSOR_COUNT
};

enum sensor_id { LID_ACCEL = 0, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

enum ioex_port { IOEX_C0_NCT38XX = 0, IOEX_PORT_COUNT };

enum battery_type {
	BATTERY_SIMPLO_HIGHPOWER,
	BATTERY_COSMX,
	BATTERY_TYPE_COUNT
};

enum pwm_channel {
	PWM_CH_LED2 = 0, /* PWM0 (white charger) */
	PWM_CH_LED3, /* PWM1 (orange on DB) */
	PWM_CH_LED1, /* PWM2 (orange charger) */
	PWM_CH_KBLIGHT, /* PWM3 */
	PWM_CH_FAN, /* PWM5 */
	PWM_CH_LED4, /* PWM7 (white on DB) */
	PWM_CH_COUNT
};

enum fan_channel { FAN_CH_0 = 0, FAN_CH_COUNT };

enum mft_channel { MFT_CH_0 = 0, MFT_CH_COUNT };

void motion_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
