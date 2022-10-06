/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Osiris board configuration */

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

/* Tablet mode is not supported */
#undef CONFIG_TABLET_MODE
#undef CONFIG_TABLET_MODE_SWITCH
#undef CONFIG_LID_ANGLE

/* LED */
#define CONFIG_LED_ONOFF_STATES

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

#undef CONFIG_USB_PD_TCPM_NCT38XX
#define CONFIG_USB_PD_TCPM_PS8815 /* C0 and C1 */

#define CONFIG_USB_PD_FRS_PPC

#define CONFIG_USB_PD_TCPM_PS8815
#define CONFIG_USB_PD_TCPM_PS8815_FORCE_DID

/* Retimer */
#undef CONFIG_USBC_RETIMER_INTEL_BB

/* I2C speed console command */
#define CONFIG_CMD_I2C_SPEED

/* I2C control host command */
#define CONFIG_HOSTCMD_I2C_CONTROL

#define CONFIG_USBC_PPC_SYV682X

/* TODO: b/177608416 - measure and check these values on osiris */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/*
 * Passive USB-C cables only support up to 60W.
 */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 60000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_PWR_STATE_DISCHARGE_FULL

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

/* I2C Bus Configuration */

#define I2C_PORT_RGBKB NPCX_I2C_PORT0_0

#define I2C_PORT_USB_C0_C2_TCPC NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1_TCPC NPCX_I2C_PORT4_1

#define I2C_PORT_USB_C0_C2_PPC NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_PPC NPCX_I2C_PORT6_1

#define I2C_PORT_USB_C0_C2_BC12 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_BC12 NPCX_I2C_PORT6_1

#define I2C_PORT_USB_C1_MUX NPCX_I2C_PORT6_1

#define I2C_PORT_BATTERY NPCX_I2C_PORT5_0
#define I2C_PORT_CHARGER NPCX_I2C_PORT7_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_PORT_MP2964 NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS 0x50

#define I2C_ADDR_MP2964_FLAGS 0x20

/* Thermal features */
#define CONFIG_THERMISTOR
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

#define CONFIG_FANS FAN_CH_COUNT
#define CONFIG_FAN_RPM_CUSTOM

/* Charger defines */
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGE_RAMP_SW
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10

/* shutdown if soc <= 3%, default is 4% */
#undef CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE
#define CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE 3

#undef CONFIG_VOLUME_BUTTONS

/* RGB Keyboard */
#define CONFIG_KEYBOARD_BACKLIGHT
#define GPIO_RGBKBD_SDB_L GPIO_EC_KB_BL_EN_L
#ifdef SECTION_IS_RW
#define CONFIG_RGB_KEYBOARD
#define CONFIG_LED_DRIVER_IS31FL3733B /* is31fl3733b on I2C */
#endif
#define RGB_GRID0_COL 12
#define RGB_GRID0_ROW 1
#define I2C_PORT_KBMCU I2C_PORT_RGBKB

#define CONFIG_KEYBOARD_REFRESH_ROW3

#define CONFIG_KEYBOARD_FACTORY_TEST

#ifndef __ASSEMBLER__

#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"
#include "usbc_config.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1_DDR_SOC,
	ADC_TEMP_SENSOR_2_AMBIENT,
	ADC_TEMP_SENSOR_3_CHARGER,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1_DDR_SOC,
	TEMP_SENSOR_2_AMBIENT,
	TEMP_SENSOR_3_CHARGER,
	TEMP_SENSOR_COUNT
};

enum battery_type { BATTERY_COSMX_AP22ABN, BATTERY_TYPE_COUNT };

enum pwm_channel {
	PWM_CH_FAN = 0, /* PWM5 */
	PWM_CH_FAN2, /* PWM3 */
	PWM_CH_COUNT
};

enum fan_channel { FAN_CH_0 = 0, FAN_CH_1, FAN_CH_COUNT };

enum mft_channel { MFT_CH_0 = 0, MFT_CH_1, MFT_CH_COUNT };

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
extern const int keyboard_factory_scan_pins[][2];
extern const int keyboard_factory_scan_pins_used;
#endif

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
