/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Endeavour board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#undef CONFIG_SYSTEM_UNLOCKED
#define CONFIG_USB_PD_COMM_LOCKED

/* EC */
#define CONFIG_ADC
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_BOARD_HAS_RTC_RESET
#define CONFIG_CRC8
#define CONFIG_CBI_EEPROM
#define CONFIG_DEDICATED_RECOVERY_BUTTON
#define CONFIG_EMULATED_SYSRQ
#define CONFIG_LED_COMMON
#define CONFIG_MKBP_INPUT_DEVICES
#define CONFIG_MKBP_USE_HOST_EVENT
#define CONFIG_DPTF
#define CONFIG_FLASH_SIZE_BYTES 0x80000
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#undef CONFIG_LID_SWITCH
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_PWM
#define CONFIG_LTO
#define CONFIG_CHIP_PANIC_BACKUP
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25X40
#define CONFIG_WATCHDOG_HELP
#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND \
	(EC_WIRELESS_SWITCH_WLAN | EC_WIRELESS_SWITCH_WLAN_POWER)
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_DX_WLAN
#define CONFIG_FANS 1
#define CONFIG_FAN_RPM_CUSTOM
#define CONFIG_THROTTLE_AP
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_PWM

/* EC console commands */
#define CONFIG_CMD_BUTTON

/* SOC */
#define CONFIG_CHIPSET_SKYLAKE
#define CONFIG_CHIPSET_HAS_PLATFORM_PMIC_RESET
#define CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S3
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 1000
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_BUTTON_INIT_IDLE
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30
#define CONFIG_DELAY_DSW_PWROK_TO_PWRBTN

/* Sensor */
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP432

/* USB-A config */
#define CONFIG_USB_PORT_POWER_DUMB
#define USB_PORT_COUNT 4

/* Optional feature to configure npcx chip */
#define NPCX_UART_MODULE2 1 /* 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2 0 /* 0:GPIO21/17/16/20 as JTAG */
#define NPCX_TACH_SEL2 1 /* 0:GPIO40/73 1:GPIO93/A6 as TACH */

/* I2C ports */
#define I2C_PORT_PSE NPCX_I2C_PORT0_0
#define I2C_PORT_EEPROM NPCX_I2C_PORT0_1
#define I2C_PORT_PMIC NPCX_I2C_PORT2
#define I2C_PORT_THERMAL NPCX_I2C_PORT3

/* I2C addresses */
#define I2C_ADDR_EEPROM_FLAGS 0x50

#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum charge_port {
	CHARGE_PORT_BARRELJACK,
};

enum temp_sensor_id {
	TEMP_SENSOR_CHARGER, /* BD99992GW SYSTHERM1 */
	TEMP_SENSOR_DRAM, /* BD99992GW SYSTHERM2 */
	TEMP_SENSOR_COUNT
};

enum adc_channel { ADC_VBUS, ADC_CH_COUNT };

enum pwm_channel {
	PWM_CH_LED_RED,
	PWM_CH_LED_WHITE,
	PWM_CH_FAN,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum fan_channel {
	FAN_CH_0,
	/* Number of FAN channels */
	FAN_CH_COUNT
};

enum mft_channel {
	MFT_CH_0,
	/* Number of MFT channels */
	MFT_CH_COUNT
};

enum OEM_ID {
	OEM_ENDEAVOUR = 9,
	/* Number of OEM IDs */
	OEM_COUNT
};

/* Board specific handlers */
void show_critical_error(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
