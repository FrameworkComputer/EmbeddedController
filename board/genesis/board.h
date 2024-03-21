/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Puff board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* NPCX7 config */
#define NPCX7_PWM1_SEL 0 /* GPIO C2 is not used as PWM1. */
#define NPCX_UART_MODULE2 1 /* GPIO64/65 are used as UART pins. */

/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Defines */
#define CONFIG_ADC
#define CONFIG_BOARD_HAS_RTC_RESET
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_DEDICATED_RECOVERY_BUTTON
#define CONFIG_DEDICATED_RECOVERY_BUTTON_2
#define CONFIG_BUTTONS_RUNTIME_CONFIG
#define CONFIG_BOARD_RESET_AFTER_POWER_ON
/* TODO: (b/143496253) re-enable CEC */
/* #define CONFIG_CEC */
#define CONFIG_CRC8
#define CONFIG_CBI_EEPROM
#define CONFIG_EMULATED_SYSRQ
#undef CONFIG_KEYBOARD_BOOT_KEYS
#define CONFIG_MKBP_INPUT_DEVICES
#define CONFIG_MKBP_USE_HOST_EVENT
#undef CONFIG_KEYBOARD_RUNTIME_KEYS
#undef CONFIG_HIBERNATE
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_LED_COMMON
#undef CONFIG_LID_SWITCH
#define CONFIG_LTO
#define CONFIG_PWM
#define CONFIG_VBOOT_EFS2
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define CONFIG_SHA256_SW

/* EC Commands */
#define CONFIG_CMD_BUTTON
/* Include CLI command needed to support CCD testing. */
#define CONFIG_CMD_CHARGEN
#undef CONFIG_CMD_FASTCHARGE
#undef CONFIG_CMD_KEYBOARD
#define CONFIG_HOSTCMD_PD_CONTROL
#undef CONFIG_CMD_PWR_AVG
#define CONFIG_CMD_PPC_DUMP
#define CONFIG_CMD_TCPC_DUMP
#ifdef SECTION_IS_RO
/* Reduce RO size by removing less-relevant commands. */
#undef CONFIG_CMD_APTHROTTLE
#undef CONFIG_CMD_CHARGEN
#undef CONFIG_CMD_HCDEBUG
#undef CONFIG_CMD_MMAPINFO
#endif

#undef CONFIG_CONSOLE_CMDHELP

/* Don't generate host command debug by default */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* Enable AP Reset command for TPM with old firmware version to detect it. */
#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_HOSTCMD_AP_RESET

/* Chipset config */
#define CONFIG_CHIPSET_COMETLAKE_DISCRETE
/* check */
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW

#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_POWER_BUTTON_X86
/* Check: */
#define CONFIG_POWER_BUTTON_INIT_IDLE
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30
#define CONFIG_DELAY_DSW_PWROK_TO_PWRBTN
#define CONFIG_POWER_PP5000_CONTROL
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_INA3221

/* Fan and temp. */
#define CONFIG_FANS 1
#undef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 0
#define CONFIG_FAN_RPM_CUSTOM
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B
#define CONFIG_THROTTLE_AP

#define CONFIG_USB_PD_PORT_MAX_COUNT 0

/* USB Type A Features */
#define CONFIG_USB_PORT_POWER_DUMB
/* There are five ports, but power enable is ganged across all of them. */
#define USB_PORT_COUNT 1

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define I2C_PORT_INA NPCX_I2C_PORT0_0
#define I2C_PORT_PPC0 NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC0 NPCX_I2C_PORT3_0
#define I2C_PORT_PSE NPCX_I2C_PORT4_1
#define I2C_PORT_POWER NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM_FLAGS 0x50

#define PP5000_PGOOD_POWER_SIGNAL_MASK POWER_SIGNAL_MASK(PP5000_A_PGOOD)

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_SNS_PP3300, /* ADC2 */
	ADC_SNS_PP1050, /* ADC7 */
	ADC_VBUS, /* ADC4 */
	ADC_PPVAR_IMON, /* ADC9 */
	ADC_TEMP_SENSOR_1, /* ADC0 */
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_FAN,
	PWM_CH_LED_RED,
	PWM_CH_LED_WHITE,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum fan_channel {
	FAN_CH_0,
	/* Number of FAN channels */
	FAN_CH_COUNT
};

enum mft_channel {
	MFT_CH_0 = 0,
	/* Number of MFT channels */
	MFT_CH_COUNT,
};

enum temp_sensor_id { TEMP_SENSOR_CORE, TEMP_SENSOR_COUNT };

/* Board specific handlers */
void led_alert(int enable);
void show_critical_error(void);

/*
 * firmware config fields
 */
/*
 * Barrel-jack power (4 bits).
 */
#define EC_CFG_BJ_POWER_L 0
#define EC_CFG_BJ_POWER_H 3
#define EC_CFG_BJ_POWER_MASK GENMASK(EC_CFG_BJ_POWER_H, EC_CFG_BJ_POWER_L)
/*
 * USB Connector 4 not present (1 bit).
 */
#define EC_CFG_NO_USB4_L 4
#define EC_CFG_NO_USB4_H 4
#define EC_CFG_NO_USB4_MASK GENMASK(EC_CFG_NO_USB4_H, EC_CFG_NO_USB4_L)
/*
 * Thermal solution config (3 bits).
 */
#define EC_CFG_THERMAL_L 5
#define EC_CFG_THERMAL_H 7
#define EC_CFG_THERMAL_MASK GENMASK(EC_CFG_THERMAL_H, EC_CFG_THERMAL_L)

unsigned int ec_config_get_thermal_solution(void);

#endif /* !__ASSEMBLER__ */

/* Pin renaming */
#define GPIO_WP_L GPIO_EC_WP_ODL
#define GPIO_PP5000_A_PG_OD GPIO_PG_PP5000_A_OD
#define GPIO_EN_PP5000 GPIO_EN_PP5000_A
#define GPIO_RECOVERY_L GPIO_EC_RECOVERY_BTN_ODL
#define GPIO_RECOVERY_L_2 GPIO_H1_EC_RECOVERY_BTN_ODL
#define GPIO_POWER_BUTTON_L GPIO_H1_EC_PWR_BTN_ODL
#define GPIO_PCH_WAKE_L GPIO_EC_PCH_WAKE_ODL
#define GPIO_PCH_PWRBTN_L GPIO_EC_PCH_PWR_BTN_ODL
#define GPIO_ENTERING_RW GPIO_EC_ENTERING_RW
#define GPIO_SYS_RESET_L GPIO_SYS_RST_ODL
#define GPIO_PCH_RSMRST_L GPIO_EC_PCH_RSMRST_L
#define GPIO_CPU_PROCHOT GPIO_EC_PROCHOT_ODL
#define GPIO_PCH_RTCRST GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SYS_PWROK GPIO_EC_PCH_SYS_PWROK
#define GPIO_PCH_SLP_S0_L GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S4_L GPIO_SLP_S4_L
#define GPIO_TEMP_SENSOR_POWER GPIO_EN_ROA_RAILS
#define GPIO_AC_PRESENT GPIO_BJ_ADP_PRESENT_L

/*
 * There is no RSMRST input, so alias it to the output. This short-circuits
 * common_intel_x86_handle_rsmrst.
 */
#define GPIO_PG_EC_RSMRST_ODL GPIO_PCH_RSMRST_L

#endif /* __CROS_EC_BOARD_H */
