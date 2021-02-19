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
#define EC_CMD_FACTORY_MODE	0x3E02

enum ec_flash_notified_flags {
	/* Enable/Disable power button pulses for x86 devices */
	FLASH_FIRMWARE_START  = BIT(0),
	FLASH_FIRMWARE_DONE   = BIT(1),
	FLASH_FLAG_PD         = BIT(4),
};

struct ec_params_flash_notified {
	/* See enum ec_flash_notified_flags */
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

struct ec_params_factory_notified {
	/* factory mode enable */
	uint8_t flags;
} __ec_align1;

#define EC_CMD_PWM_GET_FAN_ACTUAL_RPM	0x3E04

struct ec_response_pwm_get_actual_fan_rpm {
	uint32_t rpm;
} __ec_align4;

#define EC_CMD_SET_AP_REBOOT_DELAY	0x3E05

struct ec_response_ap_reboot_delay {
	uint8_t delay;
} __ec_align1;

#endif /* __HOST_COMMAND_CUSTOMIZATION_H */