/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Vell board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "compile_time_macros.h"

/* Baseboard features */
#include "baseboard.h"

/* Increase tx buffer size. */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192

/*
 * This will happen automatically on NPCX9 ES2 and later. Do not remove
 * until we can confirm all earlier chips are out of service.
 */
#define CONFIG_HIBERNATE_PSL_VCC1_RST_WAKEUP

/* No side buttons */
#undef CONFIG_MKBP_INPUT_DEVICES
#undef CONFIG_VOLUME_BUTTONS

/* Sensors */
#undef CONFIG_TABLET_MODE
#undef CONFIG_TABLET_MODE_SWITCH
#undef CONFIG_GMR_TABLET_MODE

/* TCS3400 ALS */
#define CONFIG_ALS
#define ALS_COUNT 1
#define CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(CLEAR_ALS)

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY

#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_NCT38XX
#define CONFIG_IO_EXPANDER_PORT_COUNT 4

#define CONFIG_USB_PD_FRS_PPC

#define CONFIG_USBC_RETIMER_INTEL_BB

#define CONFIG_USBC_PPC_SYV682X

#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50

#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 4

/* TODO: b/177608416 - measure and check these values on brya */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* USB Type C and USB PD defines */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_CURRENT_MA 5000
#define PD_MAX_VOLTAGE_MV 20000
/* Max Power = 100 W */
#define PD_MAX_POWER_MW ((PD_MAX_VOLTAGE_MV * PD_MAX_CURRENT_MA) / 1000)

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
#define GPIO_RIGHT_LED_AMBER_L GPIO_LED_1_L
#define GPIO_RIGHT_LED_WHITE_L GPIO_LED_2_L
#define GPIO_LEFT_LED_AMBER_L GPIO_LED_3_L
#define GPIO_LEFT_LED_WHITE_L GPIO_LED_4_L

/* System has back-lit keyboard */
#define CONFIG_PWM_KBLIGHT

/* I2C Bus Configuration */

#define I2C_PORT_SENSOR NPCX_I2C_PORT0_0

#define I2C_PORT_USB_C0_C1_TCPC NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C2_C3_TCPC NPCX_I2C_PORT4_1

#define I2C_PORT_USB_C0_C1_PPC NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C2_C3_PPC NPCX_I2C_PORT6_1

#define I2C_PORT_USB_C0_C1_BC12 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C2_C3_BC12 NPCX_I2C_PORT6_1

#define I2C_PORT_USB_C0_C1_MUX NPCX_I2C_PORT3_0
#define I2C_PORT_USB_C2_C3_MUX NPCX_I2C_PORT3_0

#define I2C_PORT_BATTERY NPCX_I2C_PORT5_0
#define I2C_PORT_CHARGER NPCX_I2C_PORT7_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS 0x50

/*
 * see b/174768555#comment22
 */
#define USBC_PORT_C0_BB_RETIMER_I2C_ADDR 0x56
#define USBC_PORT_C1_BB_RETIMER_I2C_ADDR 0x57
#define USBC_PORT_C2_BB_RETIMER_I2C_ADDR 0x58
#define USBC_PORT_C3_BB_RETIMER_I2C_ADDR 0x59

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

/* Fan features */
#define CONFIG_FANS FAN_CH_COUNT
#define CONFIG_FAN_BYPASS_SLOW_RESPONSE
#define CONFIG_CUSTOM_FAN_CONTROL
#define RPM_DEVIATION 1

/* Charger defines */
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGE_RAMP_SW
#define CONFIG_CHARGER_SENSE_RESISTOR 5
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10

/* Keyboard features */
#define CONFIG_KEYBOARD_FACTORY_TEST
#define CONFIG_KEYBOARD_REFRESH_ROW3

#undef CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE
#define CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE 3

/*
 * Older boards have a different ADC assignment.
 */

#define CONFIG_ADC_CHANNELS_RUNTIME_CONFIG
#define CONFIG_POWER_SIGNAL_RUNTIME_CONFIG

#ifndef __ASSEMBLER__

#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"
#include "usbc_config.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1_SOC,
	ADC_TEMP_SENSOR_2_CHARGER,
	ADC_TEMP_SENSOR_3_WWAN,
	ADC_TEMP_SENSOR_4_DDR,
	ADC_TEMP_SENSOR_5_REGULATOR,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1_SOC,
	TEMP_SENSOR_2_CHARGER,
	TEMP_SENSOR_3_WWAN,
	TEMP_SENSOR_4_DDR,
	TEMP_SENSOR_5_REGULATOR,
	TEMP_SENSOR_COUNT
};

enum sensor_id { CLEAR_ALS = 0, RGB_ALS, SENSOR_COUNT };

enum ioex_port {
	IOEX_C0_NCT38XX = 0,
	IOEX_C1_NCT38XX,
	IOEX_C2_NCT38XX,
	IOEX_C3_NCT38XX,
	IOEX_PORT_COUNT
};

enum battery_type {
	BATTERY_SIMPLO_COS,
	BATTERY_SIMPLO_COS2,
	BATTERY_TYPE_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0, /* PWM3 */
	PWM_CH_FAN, /* PWM5 */
	PWM_CH_COUNT
};

enum fan_channel { FAN_CH_0 = 0, FAN_CH_COUNT };

enum mft_channel { MFT_CH_0 = 0, MFT_CH_COUNT };

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
extern const int keyboard_factory_scan_pins[][2];
extern const int keyboard_factory_scan_pins_used;
#endif

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
