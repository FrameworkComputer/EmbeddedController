/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nocturne board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

#define CONFIG_SUPPRESSED_HOST_COMMANDS \
	EC_CMD_CONSOLE_SNAPSHOT, EC_CMD_CONSOLE_READ, EC_CMD_PD_GET_LOG_ENTRY

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* No tach. */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */
#define CONFIG_HIBERNATE_PSL

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE (512 * 1024) /* It's really 1MB. */
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC modules */
#define CONFIG_ADC
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BOARD_VERSION_CUSTOM
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_I2C
#define CONFIG_I2C_BUS_MAY_BE_UNPOWERED
#define CONFIG_I2C_MASTER
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

#define CONFIG_DETACHABLE_BASE

/* EC console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_BUTTON
#define CONFIG_CMD_PD_CONTROL
#define CONFIG_CMD_PPC_DUMP

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BAT_PRESENT_L

/* Buttons / Switches */
#define CONFIG_BASE_ATTACHED_SWITCH
#define CONFIG_BUTTON_TRIGGERED_RECOVERY
#define CONFIG_VOLUME_BUTTONS

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 128
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#define CONFIG_EXTPOWER_GPIO

/* LEDs */
#define CONFIG_LED_COMMON
#define CONFIG_LED_PWM_ACTIVE_CHARGE_PORT_ONLY
#define CONFIG_LED_PWM_COUNT 2
#undef CONFIG_LED_PWM_NEAR_FULL_COLOR
#define CONFIG_LED_PWM_NEAR_FULL_COLOR EC_LED_COLOR_WHITE

/* MKBP */
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_EVENT_WAKEUP_MASK 0
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT

/* Sensors */
#define CONFIG_ALS
#define ALS_COUNT 1
#define CONFIG_ALS_OPT3001
#define OPT3001_I2C_ADDR_FLAGS OPT3001_I2C_ADDR1_FLAGS
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* Must be a power of 2 */
#define CONFIG_ACCEL_FIFO_SIZE 512
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)
#define CONFIG_SYNC
#define CONFIG_SYNC_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_BD99992GW
#define CONFIG_THERMISTOR_NCP15WB

/* SoC */
#define CONFIG_BOARD_HAS_RTC_RESET
#define CONFIG_CHIPSET_SKYLAKE
#define CONFIG_CHIPSET_HAS_PLATFORM_PMIC_RESET
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_DPTF
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* USB PD */
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_PS8805
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* Define typical operating power and max power. */
#define PD_MAX_VOLTAGE_MV     20000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_POWER_MW       45000
#define PD_OPERATING_POWER_MW 15000
#define PD_VCONN_SWAP_DELAY   5000 /* us */

/* TODO(aaboagye): Verify these timings. */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY   30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY  250000 /* us */

/* I2C config */
#define I2C_PORT_CHARGER  I2C_PORT_POWER
#define I2C_PORT_PMIC     I2C_PORT_POWER
#define I2C_PORT_POWER    NPCX_I2C_PORT0_0
#define I2C_PORT_BATTERY  NPCX_I2C_PORT4_1
#define I2C_PORT_ALS_GYRO NPCX_I2C_PORT5_0
#define I2C_PORT_ACCEL    I2C_PORT_ALS_GYRO
#define I2C_PORT_USB_C0   NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1   NPCX_I2C_PORT2_0
#define I2C_PORT_THERMAL  I2C_PORT_PMIC

#define GPIO_USB_C0_SCL GPIO_EC_I2C1_USB_C0_SCL
#define GPIO_USB_C0_SDA GPIO_EC_I2C1_USB_C0_SDA
#define GPIO_USB_C1_SCL GPIO_EC_I2C2_USB_C1_SCL
#define GPIO_USB_C1_SDA GPIO_EC_I2C2_USB_C1_SDA

#define I2C_ADDR_MP2949_FLAGS  0x20
#define I2C_ADDR_BD99992_FLAGS 0x30

/*
 * Remapping of schematic GPIO names to common GPIO names expected (hardcoded)
 * in the EC code base.
 */
#define GPIO_AC_PRESENT       GPIO_ACOK_OD
#define GPIO_ENABLE_BACKLIGHT GPIO_EC_BL_DISABLE_ODL
#define GPIO_BAT_PRESENT_L    GPIO_EC_BATT_PRES_L
#define GPIO_ENTERING_RW      GPIO_EC_ENTERING_RW
#define GPIO_PCH_PWRBTN_L     GPIO_EC_PCH_PWR_BTN_L
#define GPIO_PCH_RSMRST_L     GPIO_RSMRST_L
#define GPIO_PCH_RTCRST       GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SLP_S0_L     GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L     GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S4_L     GPIO_SLP_S4_L
#define GPIO_PCH_SLP_SUS_L    GPIO_SLP_SUS_L_PCH
#define GPIO_PCH_WAKE_L       GPIO_EC_PCH_WAKE_L
#define GPIO_PMIC_DPWROK      GPIO_ROP_DSW_PWROK_EC
#define GPIO_PMIC_SLP_SUS_L   GPIO_SLP_SUS_L_PMIC
#define GPIO_POWER_BUTTON_L   GPIO_EC_PWR_BTN_IN_ODL
#define GPIO_CPU_PROCHOT      GPIO_EC_PROCHOT_ODL
#define GPIO_RSMRST_L_PGOOD   GPIO_ROP_EC_RSMRST_L
#define GPIO_VOLUME_UP_L      GPIO_H1_EC_VOL_UP_ODL
#define GPIO_VOLUME_DOWN_L    GPIO_H1_EC_VOL_DOWN_ODL
#define GPIO_WP_L             GPIO_EC_WP_L

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_BASE_ATTACH,
	ADC_BASE_DETACH,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,	/* BD99956GW TSENSE */
	TEMP_SENSOR_AMBIENT,	/* BD99992GW SYSTHERM0 */
	TEMP_SENSOR_CHARGER,	/* BD99992GW SYSTHERM1 */
	TEMP_SENSOR_DRAM,	/* BD99992GW SYSTHERM2 */
	TEMP_SENSOR_EMMC,	/* BD99992GW SYSTHERM3 */
	TEMP_SENSOR_GYRO,	/* BMI160 */
	TEMP_SENSOR_COUNT
};

enum pwm_channel {
	PWM_CH_DB0_LED_RED = 0,
	PWM_CH_DB0_LED_GREEN,
	PWM_CH_DB0_LED_BLUE,
	PWM_CH_DB1_LED_RED,
	PWM_CH_DB1_LED_GREEN,
	PWM_CH_DB1_LED_BLUE,
	PWM_CH_COUNT
};

/*
 * Motion sensors:
 * When reading through IO memory is set up for sensors (LPC is used),
 * the first 2 entries must be accelerometers, then gyroscope.
 * For BMI160, accel and gyro sensors must be next to each other.
 */
enum sensor_id {
	LID_ACCEL,
	LID_GYRO,
	LID_ALS,
	VSYNC,
	SENSOR_COUNT,
};

#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ALS)

void base_pwr_fault_interrupt(enum gpio_signal s);
int board_get_version(void);

/* Reset all TCPCs. */
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);

#endif /* __ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
