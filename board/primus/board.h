/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Primus board configuration */

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
#undef CONFIG_TABLET_MODE
#undef CONFIG_TABLET_MODE_SWITCH
#undef CONFIG_GMR_TABLET_MODE

/* No side buttons */
#undef CONFIG_MKBP_INPUT_DEVICES
#undef CONFIG_VOLUME_BUTTONS

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY

#undef CONFIG_USB_PD_TCPM_NCT38XX
#define CONFIG_USB_PD_TCPM_RT1715
#define CONFIG_USBC_RETIMER_INTEL_BB

#define CONFIG_USBC_PPC_SYV682X

#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/*
 * Passive USB-C cables only support up to 60W.
 */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 65000
#define PD_MAX_CURRENT_MA 3250
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
#define GPIO_WP_L GPIO_EC_WP_ODL

#define GPIO_ID_1_EC_KB_BL_EN GPIO_EC_BATT_PRES_ODL

/* System has back-lit keyboard */
#define CONFIG_PWM_KBLIGHT

/* Keyboard features */
#define CONFIG_KEYBOARD_REFRESH_ROW3

/* I2C Bus Configuration */

#define I2C_PORT_USB_C0_TCPC NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1_TCPC NPCX_I2C_PORT4_1
#define I2C_PORT_USB_C0_C1_PPC_BC NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C0_C1_BC12 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C0_C1_RT NPCX_I2C_PORT3_0
#define I2C_PORT_BATTERY NPCX_I2C_PORT5_0
#define I2C_PORT_USB_A0_A1_MIX NPCX_I2C_PORT6_1
#define I2C_PORT_CHARGER NPCX_I2C_PORT7_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_PORT_MP2964 NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS 0x50

#define I2C_ADDR_MP2964_FLAGS 0x20

#define USBC_PORT_C0_BB_RETIMER_I2C_ADDR 0x56
#define USBC_PORT_C1_BB_RETIMER_I2C_ADDR 0x57

/* Enabling Thunderbolt-compatible mode */
#define CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enabling USB4 mode */
#define CONFIG_USB_PD_USB4

/* Retimer */
#define CONFIG_USBC_RETIMER_FW_UPDATE

/* Thermal features */
#define CONFIG_THERMISTOR
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

/* Fan features */
#define CONFIG_CUSTOM_FAN_CONTROL
#define CONFIG_FANS FAN_CH_COUNT
#define RPM_DEVIATION 1

/* Charger defines */
#define CONFIG_CHARGER_BQ25720
#define CONFIG_CHARGER_BQ25720_VSYS_TH2_CUSTOM
#define CONFIG_CHARGER_BQ25720_VSYS_TH2_DV 70
#define CONFIG_CHARGE_RAMP_SW
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR 10
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR_AC 10
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT

/* PROCHOT defines */
#define BATT_MAX_CONTINUE_DISCHARGE_WATT 66

/* Prochot assertion/deassertion ratios*/
#define PROCHOT_ADAPTER_WATT_RATIO 97
#define PROCHOT_ASSERTION_BATTERY_RATIO 95
#define PROCHOT_DEASSERTION_BATTERY_RATIO 85
#define PROCHOT_ASSERTION_PD_RATIO 105
#define PROCHOT_DEASSERTION_PD_RATIO 100
#define PROCHOT_DEASSERTION_PD_BATTERY_RATIO 95
#define PROCHOT_ASSERTION_ADAPTER_RATIO 105
#define PROCHOT_DEASSERTION_ADAPTER_RATIO 100
#define PROCHOT_DEASSERTION_ADAPTER_BATT_RATIO 90

/* PS2 defines */
#define CONFIG_8042_AUX
#define CONFIG_PS2
#define CONFIG_CMD_PS2
#define PRIMUS_PS2_CH NPCX_PS2_CH1

#ifndef __ASSEMBLER__

#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"
#include "usbc_config.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1_DDR_SOC,
	ADC_TEMP_SENSOR_2_SSD,
	ADC_TEMP_SENSOR_3_CHARGER,
	ADC_TEMP_SENSOR_4_MEMORY,
	ADC_TEMP_SENSOR_5_USBC,
	ADC_IADPT,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1_DDR_SOC,
	TEMP_SENSOR_2_SSD,
	TEMP_SENSOR_3_CHARGER,
	TEMP_SENSOR_4_MEMORY,
	TEMP_SENSOR_5_USBC,
	TEMP_SENSOR_COUNT
};

enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	CLEAR_ALS,
	RGB_ALS,
	SENSOR_COUNT
};

enum battery_type {
	BATTERY_SUNWODA_5B11F21946,
	BATTERY_SUNWODA_5B11H56342,
	BATTERY_SMP_5B11F21953,
	BATTERY_SMP_5B11H56344,
	BATTERY_CELXPERT_5B11F21941,
	BATTERY_CELXPERT_5B11H56343,
	BATTERY_CELXPERT_5B11M90007,
	BATTERY_SMP_5B11M90006,
	BATTERY_SUNWODA_5B11M90008,
	BATTERY_TYPE_COUNT
};

enum pwm_channel {
	PWM_CH_LED2_WHITE = 0, /* PWM0 (white charger) */
	PWM_CH_TKP_A_LED_N, /* PWM1 (LOGO led on A cover) */
	PWM_CH_LED1_AMBER, /* PWM2 (orange charger) */
	PWM_CH_KBLIGHT, /* PWM3 */
	PWM_CH_FAN, /* PWM5 */
	PWM_CH_LED4, /* PWM7 (power) */
	PWM_CH_COUNT
};

enum fan_channel { FAN_CH_0 = 0, FAN_CH_COUNT };

enum mft_channel { MFT_CH_0 = 0, MFT_CH_COUNT };

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
