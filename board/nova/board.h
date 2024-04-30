/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brask board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "compile_time_macros.h"

/* Baseboard features */
#include "baseboard.h"

#define CONFIG_MP2964

/* Barrel Jack */
#define DEDICATED_CHARGE_PORT 0

/* HDMI CEC */
#define CONFIG_CEC
#define CONFIG_CEC_BITBANG

/* USB Type A Features */
#define USB_PORT_COUNT 2
#define CONFIG_USB_PORT_POWER_DUMB

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_AC_PRESENT GPIO_ACOK_OD
#define GPIO_CPU_PROCHOT GPIO_EC_PROCHOT_ODL
#define GPIO_EC_INT_L GPIO_EC_PCH_INT_ODL
#define GPIO_ENTERING_RW GPIO_EC_ENTERING_RW
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
#define GPIO_WP_L GPIO_EC_WP_ODL
#define GPIO_RECOVERY_L GPIO_EC_RECOVERY_BTN_OD
#define GPIO_RECOVERY_L_2 GPIO_GSC_EC_RECOVERY_BTN_OD

/* I2C Bus Configuration */
#define I2C_PORT_SENSOR NPCX_I2C_PORT3_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_PORT_MP2964 NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS 0x50

#define I2C_ADDR_MP2964_FLAGS 0x20

/* USBC BC1.2 */
#undef CONFIG_USB_CHARGER
#undef CONFIG_BC12_DETECT_PI3USB9201

/* USB Type C and USB PD defines */
#undef CONFIG_USB_PD_TCPMV2
#undef CONFIG_USB_DRP_ACC_TRYSRC
#undef CONFIG_USB_PD_REV30

#undef CONFIG_CMD_HCDEBUG
#undef CONFIG_CMD_PPC_DUMP
#undef CONFIG_CMD_TCPC_DUMP

#undef CONFIG_USB_POWER_DELIVERY
#undef CONFIG_USB_PD_ALT_MODE
#undef CONFIG_USB_PD_ALT_MODE_DFP
#undef CONFIG_USB_PD_ALT_MODE_UFP
#undef CONFIG_USB_PD_DISCHARGE_PPC
#undef CONFIG_USB_PD_DUAL_ROLE
#undef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#undef CONFIG_USB_PD_TCPC_LOW_POWER
#undef CONFIG_USB_PD_TCPM_TCPCI
#undef CONFIG_USB_PD_TCPM_NCT38XX

#undef CONFIG_USB_PD_TCPM_MUX
#undef CONFIG_HOSTCMD_PD_CONTROL
#undef CONFIG_CMD_USB_PD_PE

#define CONFIG_USB_PD_PORT_MAX_COUNT 0

#undef CONFIG_USB_PD_USB32_DRD

#undef CONFIG_USB_PD_TRY_SRC
#undef CONFIG_USB_PD_VBUS_DETECT_TCPC

#undef CONFIG_USBC_PPC
#undef CONFIG_USBC_PPC_POLARITY
#undef CONFIG_USBC_PPC_SBU
#undef CONFIG_USBC_PPC_VCONN
#undef CONFIG_USBC_PPC_DEDICATED_INT

#undef CONFIG_USBC_SS_MUX
#undef CONFIG_USB_MUX_VIRTUAL

#undef CONFIG_USBC_VCONN
#undef CONFIG_USBC_VCONN_SWAP

#undef CONFIG_CMD_USB_PD_CABLE
#undef CONFIG_USB_PD_DECODE_SOP

#undef CONFIG_IO_EXPANDER

/* Charger */
#undef CONFIG_CHARGE_MANAGER

/* Thermal features */
#define CONFIG_THERMISTOR
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

/* ADC */
#define CONFIG_ADC

/*
 * TODO(b/197478860): Enable the fan control. We need
 * to check the sensor value and adjust the fan speed.
 */
#define CONFIG_FANS FAN_CH_COUNT

/* Include math_util for bitmask_uint64 used in pd_timers */
#define CONFIG_MATH_UTIL

/* Sensor */
#undef CONFIG_MOTION_SENSE_RESUME_DELAY_US
#define CONFIG_MOTION_SENSE_RESUME_DELAY_US (1000 * MSEC)
#define CONFIG_CMD_ACCEL_INFO
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

/* TCS3400 ALS */
#define CONFIG_ALS
#define ALS_COUNT 1
#define CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(CLEAR_ALS)

#ifndef __ASSEMBLER__

#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1_CPU,
	ADC_TEMP_SENSOR_2_CPU_VR,
	ADC_TEMP_SENSOR_3_WIFI,
	ADC_TEMP_SENSOR_4_DIMM,
	ADC_VBUS,
	ADC_PPVAR_IMON, /* ADC3 */
	ADC_CH_COUNT
};

enum sensor_id {
	CLEAR_ALS,
	RGB_ALS,
	SENSOR_COUNT,
};

enum temp_sensor_id {
	TEMP_SENSOR_1_CPU,
	TEMP_SENSOR_2_CPU_VR,
	TEMP_SENSOR_3_WIFI,
	TEMP_SENSOR_4_DIMM,
	TEMP_SENSOR_COUNT
};

enum ioex_port { IOEX_C0_NCT38XX = 0, IOEX_C2_NCT38XX, IOEX_PORT_COUNT };

enum pwm_channel {
	PWM_CH_LED_WHITE, /* PWM0 */
	PWM_CH_FAN, /* PWM5 */
	PWM_CH_LED_RED, /* PWM2 */
	PWM_CH_COUNT
};

enum fan_channel { FAN_CH_0 = 0, FAN_CH_COUNT };

enum mft_channel { MFT_CH_0 = 0, MFT_CH_COUNT };

enum cec_port { CEC_PORT_0, CEC_PORT_COUNT };

extern void adp_connect_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
