/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7483: Active redriver with linear equilzation
 *
 * Public functions, definitions, and structures.
 */

#ifndef __CROS_EC_USB_RETIMER_ANX7483_PUBLIC_H
#define __CROS_EC_USB_RETIMER_ANX7483_PUBLIC_H

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C interface addresses */
#define ANX7483_I2C_ADDR0_FLAGS 0x3E
#define ANX7483_I2C_ADDR1_FLAGS 0x38
#define ANX7483_I2C_ADDR2_FLAGS 0x40
#define ANX7483_I2C_ADDR3_FLAGS 0x44

/* Equalization tuning */
enum anx7483_eq_setting {
	ANX7483_EQ_SETTING_3_9DB = 0,
	ANX7483_EQ_SETTING_4_7DB = 1,
	ANX7483_EQ_SETTING_5_5DB = 2,
	ANX7483_EQ_SETTING_6_1DB = 3,
	ANX7483_EQ_SETTING_6_8DB = 4,
	ANX7483_EQ_SETTING_7_3DB = 5,
	ANX7483_EQ_SETTING_7_8DB = 6,
	ANX7483_EQ_SETTING_8_1DB = 7,
	ANX7483_EQ_SETTING_8_4DB = 8,
	ANX7483_EQ_SETTING_8_7DB = 9,
	ANX7483_EQ_SETTING_9_2DB = 10,
	ANX7483_EQ_SETTING_9_7DB = 11,
	ANX7483_EQ_SETTING_10_3DB = 12,
	ANX7483_EQ_SETTING_11_1DB = 13,
	ANX7483_EQ_SETTING_11_8DB = 14,
	ANX7483_EQ_SETTING_12_5DB = 15,
};

/* Flat gain tuning */
enum anx7483_fg_setting {
	ANX7483_FG_SETTING_NEG_1_5DB = 0,
	ANX7483_FG_SETTING_0_5DB = 1,
	ANX7483_FG_SETTING_0_3DB = 2,
	ANX7483_FG_SETTING_1_2DB = 3,
};

enum anx7483_tune_pin {
	ANX7483_PIN_UTX1,
	ANX7483_PIN_UTX2,
	ANX7483_PIN_URX1,
	ANX7483_PIN_URX2,
	ANX7483_PIN_DRX1,
	ANX7483_PIN_DRX2,
};

/* Adjust the equalization for a pin */
enum ec_error_list anx7483_set_eq(const struct usb_mux *me,
				  enum anx7483_tune_pin pin,
				  enum anx7483_eq_setting eq);

/* Adjust the flat gain for a pin */
enum ec_error_list anx7483_set_fg(const struct usb_mux *me,
				  enum anx7483_tune_pin pin,
				  enum anx7483_fg_setting fg);

/*
 * Configure datasheet defaults for tuning registers at this mux setting.
 * Return int so function can be used directly for board_set.
 */
int anx7483_set_default_tuning(const struct usb_mux *me, mux_state_t mux_state);

extern const struct usb_mux_driver anx7483_usb_retimer_driver;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_RETIMER_ANX7483_PUBLIC_H */
