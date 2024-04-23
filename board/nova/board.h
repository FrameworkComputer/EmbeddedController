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
#define DEDICATED_CHARGE_PORT 3

/* HDMI CEC */
#define CONFIG_CEC
#define CONFIG_CEC_BITBANG

/* USB Type A Features */
#define USB_PORT_COUNT 2
#define CONFIG_USB_PORT_POWER_DUMB

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY

#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_NCT38XX
#define CONFIG_IO_EXPANDER_PORT_COUNT 2

#define CONFIG_USB_PD_PPC
#define CONFIG_USB_PD_TCPM_RT1715
#define CONFIG_USBC_RETIMER_INTEL_BB

#define CONFIG_USBC_PPC_SYV682X
#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50

/* TODO: b/177608416 - measure and check these values on brya */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Not support typec adapter. */
/* TODO(b/293975611): Set the max PD to 0W.
 */
#define PD_OPERATING_POWER_MW CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON
#define PD_MAX_POWER_MW 0
#define PD_MAX_CURRENT_MA 0
#define PD_MAX_VOLTAGE_MV 5000

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
#define I2C_PORT_USB_C0_C2_TCPC NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1_TCPC NPCX_I2C_PORT0_0

#define I2C_PORT_USB_C0_C2_PPC NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_PPC NPCX_I2C_PORT4_1

#define I2C_PORT_USB_C0_C2_BC12 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_BC12 NPCX_I2C_PORT4_1

#define I2C_PORT_USB_C0_C2_MUX NPCX_I2C_PORT3_0

#define I2C_PORT_ADB NPCX_I2C_PORT6_1

#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_PORT_MP2964 NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS 0x50

#define I2C_ADDR_MP2964_FLAGS 0x20

#define USBC_PORT_C0_BB_RETIMER_I2C_ADDR 0x59

/* Enabling Thunderbolt-compatible mode */
#define CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enabling USB4 mode */
#define CONFIG_USB_PD_USB4
#define CONFIG_USB_PD_DATA_RESET_MSG

/* Retimer */
#define CONFIG_USBC_RETIMER_FW_UPDATE

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

#ifndef __ASSEMBLER__

#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"
#include "usbc_config.h"

enum charge_port {
	CHARGE_PORT_TYPEC0,
	CHARGE_PORT_TYPEC1,
	CHARGE_PORT_TYPEC2,
	CHARGE_PORT_BARRELJACK,
	CHARGE_PORT_ENUM_COUNT
};

enum adc_channel {
	ADC_TEMP_SENSOR_1_CPU,
	ADC_TEMP_SENSOR_2_CPU_VR,
	ADC_TEMP_SENSOR_3_WIFI,
	ADC_TEMP_SENSOR_4_DIMM,
	ADC_VBUS,
	ADC_PPVAR_IMON, /* ADC3 */
	ADC_CH_COUNT
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
