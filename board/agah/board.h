/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Agah board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "compile_time_macros.h"

/* Baseboard features */
#include "baseboard.h"

/*
 * Nvidia GPU
 */
#define CONFIG_GPU_NVIDIA

/*
 * This will happen automatically on NPCX9 ES2 and later. Do not remove
 * until we can confirm all earlier chips are out of service.
 */
#define CONFIG_HIBERNATE_PSL_VCC1_RST_WAKEUP

/*
 * Agah blocks PG_PP3300_S5_OD instead to control AP power-on.
 */
#undef CONFIG_CHIPSET_X86_RSMRST_AFTER_S5

/* Sensors */
#undef CONFIG_TABLET_MODE
#undef CONFIG_TABLET_MODE_SWITCH
#undef CONFIG_GMR_TABLET_MODE

/* Buttons */
#undef CONFIG_VOLUME_BUTTONS

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_TCPM_RT1715
#undef CONFIG_USB_PD_TCPM_NCT38XX
#define CONFIG_USBC_RETIMER_PS8818

/* I2C speed console command */
#define CONFIG_CMD_I2C_SPEED

/* I2C control host command */
#define CONFIG_HOSTCMD_I2C_CONTROL

#define CONFIG_USBC_PPC_SYV682X
#define CONFIG_USB_PD_FRS_PPC
#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50

#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 30000 /* us */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 100000
#define PD_MAX_CURRENT_MA 5000
#define PD_MAX_VOLTAGE_MV 20000

#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 500

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

/* System has back-lit keyboard */
#define CONFIG_PWM_KBLIGHT

/* I2C Bus Configuration */

#define I2C_PORT_SENSOR NPCX_I2C_PORT0_0

#define I2C_PORT_USB_C0_TCPC NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C2_TCPC NPCX_I2C_PORT2_0

#define I2C_PORT_USB_C0_PPC NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C2_PPC NPCX_I2C_PORT2_0

#define I2C_PORT_USB_C0_BC12 NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C2_BC12 NPCX_I2C_PORT2_0

#define I2C_PORT_USBA1_RT NPCX_I2C_PORT6_1

#define I2C_PORT_BATTERY NPCX_I2C_PORT5_0
#define I2C_PORT_CHARGER NPCX_I2C_PORT7_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS 0x50

/* Thermal features */
#define CONFIG_THERMISTOR
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER
#define CONFIG_TEMP_SENSOR_FIRST_READ_DELAY_MS 500
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

#define CONFIG_FANS FAN_CH_COUNT

/* Charger defines */
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
/* Round down 7700 max current to multiple of 128mA for ISL9241 AC prochot. */
#define AGAH_AC_PROCHOT_CURRENT_MA 7680

/* Barrel jack adapter settings */
#undef CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1
/* This is the next available port # after USB-C ports. */
#define DEDICATED_CHARGE_PORT 2

/*
 * Older boards have a different ADC assignment.
 */

#define CONFIG_ADC_CHANNELS_RUNTIME_CONFIG

#ifndef __ASSEMBLER__

#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"
#include "usbc_config.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1_DDR_SOC,
	ADC_TEMP_SENSOR_2_GPU,
	ADC_TEMP_SENSOR_3_CHARGER,
	ADC_CHARGER_IADP,
	ADC_ADP_TYP,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1_DDR_SOC,
	TEMP_SENSOR_2_GPU,
	TEMP_SENSOR_3_CHARGER,
	TEMP_SENSOR_COUNT
};

enum battery_type {
	BATTERY_DYNAPACK_COSMX,
	BATTERY_DYNAPACK_HIGHPOWER,
	BATTERY_TYPE_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0, /* PWM3 */
	PWM_CH_FAN, /* PWM5 */
	PWM_CH_FAN2, /* PWM4 */
	PWM_CH_COUNT
};

enum fan_channel { FAN_CH_0 = 0, FAN_CH_1, FAN_CH_COUNT };

enum mft_channel { MFT_CH_0 = 0, MFT_CH_1, MFT_CH_COUNT };

enum charge_port {
	CHARGE_PORT_TYPEC0,
	CHARGE_PORT_TYPEC1,
	CHARGE_PORT_BARRELJACK,
};

/**
 * Interrupt handler for PG_PP3300_S5_OD changes.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void board_power_interrupt(enum gpio_signal signal);

/* IRQ for BJ plug/unplug. */
void bj_present_interrupt(enum gpio_signal signal);

/* IRQ for over temperature. */
void gpu_overt_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
