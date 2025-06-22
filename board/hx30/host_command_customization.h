/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* host command customization configuration */

#ifndef __HOST_COMMAND_CUSTOMIZATION_H
#define __HOST_COMMAND_CUSTOMIZATION_H

/*****************************************************************************/
/* Configure the behavior of the flash notify */
#define EC_CMD_FLASH_NOTIFIED 0x3E01

enum ec_flash_notified_flags {
	/* Enable/Disable power button pulses for x86 devices */
	FLASH_ACCESS_SPI	  = 0,
	FLASH_FIRMWARE_START  = BIT(0),
	FLASH_FIRMWARE_DONE   = BIT(1),
	FLASH_ACCESS_SPI_DONE = 3,
	FLASH_FLAG_PD         = BIT(4),
};

struct ec_params_flash_notified {
	/* See enum ec_flash_notified_flags */
	uint8_t flags;
} __ec_align1;

/* Factory will need change Fnkey and power button
 * key scancode to test keyboard.
 */
#define EC_CMD_FACTORY_MODE	0x3E02
#define RESET_FOR_SHIP 0x5A

struct ec_params_factory_notified {
	/* factory mode enable */
	uint8_t flags;
} __ec_align1;

/* Configure the behavior of the charge limit control */
#define EC_CMD_CHARGE_LIMIT_CONTROL 0x3E03
#define NEED_RESTORE 0x7F

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

#define EC_CMD_PWM_GET_FAN_ACTUAL_RPM	0x3E04

struct ec_response_pwm_get_actual_fan_rpm {
	uint32_t rpm;
} __ec_align4;

#define EC_CMD_SET_AP_REBOOT_DELAY	0x3E05

struct ec_response_ap_reboot_delay {
	uint8_t delay;
} __ec_align1;

#define EC_CMD_ME_CONTROL	0x3E06

enum ec_mecontrol_modes {
	ME_LOCK		= BIT(0),
	ME_UNLOCK	= BIT(1),
};

struct ec_params_me_control {
	uint8_t me_mode;
} __ec_align1;

/* To notice EC enter non-acpi mode */
#define EC_CMD_CUSTOM_HELLO	0x3E07

#define EC_CMD_DISABLE_PS2_EMULATION 0x3E08

struct ec_params_ps2_emulation_control {
	uint8_t disable;
} __ec_align1;

#define EC_CMD_CHASSIS_INTRUSION 0x3E09
#define EC_PARAM_CHASSIS_INTRUSION_MAGIC 0xCE
#define EC_PARAM_CHASSIS_BBRAM_MAGIC 0xEC

struct ec_params_chassis_intrusion_control {
	uint8_t clear_magic;
	uint8_t clear_chassis_status;
} __ec_align1;

struct ec_response_chassis_intrusion_control {
	uint8_t chassis_ever_opened;			/* have rtc(VBAT) no battery(VTR) */
	uint8_t coin_batt_ever_remove;
	uint8_t total_open_count;
	uint8_t vtr_open_count;
} __ec_align1;

/* Debug LED for BIOS boot check */
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

#define EC_CMD_VPRO_CONTROL	0x3E0D

enum ec_vrpo_control_modes {
	VPRO_OFF	= 0,
	VPRO_ON		= BIT(0),
};

struct ec_params_vpro_control {
	uint8_t vpro_mode;
} __ec_align1;

#define EC_CMD_BB_RETIMER_CONTROL 0x3E0A

enum bb_retimer_control_mode {
	/* entry bb retimer firmware update mode */
	BB_ENTRY_FW_UPDATE_MODE = BIT(0),
	/* exit bb retimer firmware update mode */
	BB_EXIT_FW_UPDATE_MODE = BIT(1),
	/* enable compliance mode */
	BB_ENABLE_COMPLIANCE_MODE = BIT(2),
	/* disable compliance mode */
	BB_DISABLE_COMPLIANCE_MODE = BIT(3),
	/* Check fw update mode */
	BB_CHECK_STATUS	= BIT(7),
};

struct ec_params_bb_retimer_control_mode {
	uint8_t controller;
	/* See enum bb_retimer_control_mode */
	uint8_t modes;
} __ec_align1;

struct ec_response_bb_retimer_control_mode {
	uint8_t status;
} __ec_align1;

#define EC_CMD_FP_LED_LEVEL_CONTROL 0x3E0E

struct ec_params_fp_led_control {
	uint8_t set_led_level;
	uint8_t get_led_level;
} __ec_align1;

enum fp_led_brightness_level {
	FP_LED_BRIGHTNESS_HIGH = 0,
	FP_LED_BRIGHTNESS_MEDIUM = 1,
	FP_LED_BRIGHTNESS_LOW = 2,
};

struct ec_response_fp_led_level {
	uint8_t level;
} __ec_align1;

#define EC_CMD_CHASSIS_OPEN_CHECK 0x3E0F

struct ec_response_chassis_open_check {
	uint8_t status;
} __ec_align1;

/* To notice EC enter acpi mode */
#define EC_CMD_CUSTOM_HELLO_ACPI       0x3E10

#define EC_CMD_READ_PD_VERSION 0x3E11

struct ec_response_read_pd_version {
	uint8_t pd0_version[8];
	uint8_t pd1_version[8];
} __ec_align1;

#define EC_CMD_THERMAL_QEVENT 0x3E12

struct ec_params_thermal_qevent_control {
	uint8_t send_event;
} __ec_align1;

#define EC_CMD_STANDALONE_MODE 0x3E13

struct ec_params_standalone_mode {
	uint8_t enable;
} __ec_align1;

/* how many times did chassis(sw3) pressed */
#define EC_CMD_CHASSIS_COUNTER 0x3E15

struct ec_response_chassis_counter {
	uint8_t press_counter;
} __ec_align1;

/* If enabled, display key emits HID System Display Toggle Int/Ext Mode;
   otherwise emits Win+P */
#define EC_CMD_DISPLAY_TOGGLE_KEY_HID 0x3E16

struct ec_params_display_toggle_key_hid {
	uint8_t enable;
} __ec_align1;

#endif /* __HOST_COMMAND_CUSTOMIZATION_H */
