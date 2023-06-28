/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_HOST_COMMAND_H
#define __BOARD_HOST_COMMAND_H

#include "host_command.h"

#define SCI_HOST_EVENT_MASK					\
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_LOW) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_CRITICAL) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY)	|	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_SHUTDOWN) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_REBOOT) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_UCSI) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATT_BTP))

#define SCI_HOST_WAKE_EVENT_MASK			\
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON)	|		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) |	\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATT_BTP)	|			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEY_PRESSED))

/*****************************************************************************/
/*
 * Configure the behavior of the flash notify
 */
#define EC_CMD_FLASH_NOTIFIED		0x3E01

enum ec_flash_notified_flags {
	/* Enable/Disable power button pulses for x86 devices */
	FLASH_ACCESS_SPI	= 0,
	FLASH_FIRMWARE_START	= 1,
	FLASH_FIRMWARE_DONE	= 2,
	FLASH_ACCESS_SPI_DONE	= 3,
	FLASH_FLAG_PD		= BIT(4),
};

struct ec_params_flash_notified {
	/* See enum ec_flash_notified_flags */
	uint8_t flags;
} __ec_align1;

/*****************************************************************************/
/*
 * Factory will need change Fnkey and power button
 * key scancode to test keyboard.
 */
#define EC_CMD_FACTORY_MODE	0x3E02
#define RESET_FOR_SHIP 0x5A

struct ec_params_factory_notified {
	/* factory mode enable */
	uint8_t flags;
} __ec_align1;

/*****************************************************************************/
/*
 * Configure the behavior of the charge limit control.
 */
#define EC_CMD_CHARGE_LIMIT_CONTROL	0x3E03
#define EC_CHARGE_LIMIT_RESTORE		0x7F

enum ec_chg_limit_control_modes {
	/* Disable all setting, charge control by charge_manage */
	CHG_LIMIT_DISABLE	= BIT(0),
	/* Set maximum and minimum percentage */
	CHG_LIMIT_SET_LIMIT	= BIT(1),
	/* Host read current setting */
	CHG_LIMIT_GET_LIMIT	= BIT(3),
	/* Enable override mode, allow charge to full this time */
	CHG_LIMIT_OVERRIDE	= BIT(7),
};

struct ec_params_ec_chg_limit_control {
	/* See enum ec_chg_limit_control_modes */
	uint8_t modes;
	uint8_t max_percentage;
	uint8_t min_percentage;
} __ec_align1;

struct ec_response_chg_limit_control {
	uint8_t max_percentage;
	uint8_t min_percentage;
} __ec_align1;

/*****************************************************************************/
/*
 * Configure the behavior of the charge limit control.
 */

#define EC_CMD_PWM_GET_FAN_ACTUAL_RPM	0x3E04

struct ec_params_ec_pwm_get_actual_fan_rpm {
	/* The index of the fan */
	uint8_t index;
} __ec_align1;

struct ec_response_pwm_get_actual_fan_rpm {
	uint16_t rpm;
} __ec_align2;


/*****************************************************************************/
/*
 * This command uses to notify the EC needs to keep the pch power in s5.
 */
#define EC_CMD_SET_AP_REBOOT_DELAY	0x3E05

struct ec_response_ap_reboot_delay {
	uint8_t delay;
} __ec_align1;

/*****************************************************************************/
/*
 * This command uses to notify the EC that the system is in non-ACPI mode.
 */
#define EC_CMD_NON_ACPI_NOTIFY		0x3E07

/*****************************************************************************/
/*
 * This command uses for BIOS check Chassis.
 */
#define EC_CMD_CHASSIS_INTRUSION 0x3E09
#define EC_PARAM_CHASSIS_INTRUSION_MAGIC 0xCE
#define EC_PARAM_CHASSIS_BBRAM_MAGIC 0xEC

struct ec_params_chassis_intrusion_control {
	uint8_t clear_magic;
	uint8_t clear_chassis_status;
} __ec_align1;

struct ec_response_chassis_intrusion_control {
	uint8_t chassis_ever_opened;	/* bios used */
	uint8_t coin_batt_ever_remove;	/* factory used */
	uint8_t total_open_count;	/* reserved */
	uint8_t vtr_open_count;		/* reserved */
} __ec_align1;

/*****************************************************************************/
/*
 * This command uses for BIOS boot check.
 */
#define EC_CMD_DIAGNOSIS 0x3E0B

enum ec_params_diagnosis_code {
	/* type: DDR */
	CODE_DDR_TRAINING_START	= 1,
	CODE_DDR_TRAINING_FINISH = 2,
	CODE_DDR_FAIL = 3,
	CODE_NO_EDP = 4,
	CODE_PORT80_COMPLETE = 0xFF,
};

struct ec_params_diagnosis {
	/* See enum ec_params_diagnosis_code */
	uint8_t diagnosis_code;
} __ec_align1;

/*****************************************************************************/
/*
 * This command uses to Swap Control Fn key for the system BIOS menu option.
 */
#define EC_CMD_UPDATE_KEYBOARD_MATRIX 0x3E0C
struct keyboard_matrix_map {
	uint8_t row;
	uint8_t col;
	uint16_t scanset;
} __ec_align1;
struct ec_params_update_keyboard_matrix {
	uint32_t num_items;
	uint32_t write;
	struct keyboard_matrix_map scan_update[32];
} __ec_align1;


/*****************************************************************************/
/*
 * This command uses to change the fingerprint LED level.
 */
#define EC_CMD_FP_LED_LEVEL_CONTROL	0x3E0E

struct ec_params_fp_led_control {
	uint8_t set_led_level;
	uint8_t get_led_level;
} __ec_align1;

struct ec_response_fp_led_level {
	uint8_t level;
} __ec_align1;

/*****************************************************************************/
/*
 * This command return the chassis status
 */
#define EC_CMD_CHASSIS_OPEN_CHECK	0x3E0F

struct ec_response_chassis_open_check {
	uint8_t status;
} __ec_align1;

/*****************************************************************************/
/*
 * This command uses to notify the EC that the system is in ACPI mode.
 */
#define EC_CMD_ACPI_NOTIFY		0x3E10

/*****************************************************************************/
/*
 * This command returns the PD chip version.
 */
#define EC_CMD_READ_PD_VERSION		0x3E11

struct ec_response_read_pd_version {
	uint8_t pd0_version[8];
	uint8_t pd1_version[8];
} __ec_align1;

/*****************************************************************************/
/*
 * This command uses to control the standalone mode enable/disable
 */
#define EC_CMD_STANDALONE_MODE 0x3E13

struct ec_params_standalone_mode {
	uint8_t enable;
} __ec_align1;


/*****************************************************************************/
/*
 * This command returns how many times did chassis(sw3) pressed
 */
#define EC_CMD_CHASSIS_COUNTER 0x3E15

struct ec_response_chassis_counter {
	uint8_t press_counter;
} __ec_align1;

/*****************************************************************************/
/*
 * This command returns the input deck state and board id.
 */
#define EC_CMD_CHECK_DECK_STATE		0x3E16

struct ec_params_deck_state {
	uint8_t mode;
} __ec_align1;

struct ec_response_deck_state {
	uint8_t input_deck_board_id[8];
	uint8_t deck_state;
} __ec_align1;

/*****************************************************************************/
/*
 * This command returns the simple ec version.
 */
#define EC_CMD_GET_SIMPLE_VERSION	0x3E17

struct ec_response_get_custom_version {
	char simple_version[9];
} __ec_align4;

/*****************************************************************************/
/*
 * This command returns the active charge pd chip.
 */
#define EC_CMD_GET_ACTIVE_CHARGE_PD_CHIP	0x3E18

struct ec_response_get_active_charge_pd_chip {
	uint8_t pd_chip;
} __ec_align1;


#endif /* __BOARD_HOST_COMMAND_H */
