/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_KEYBOARD_BACKLIGHT_H
#define __CROS_EC_KEYBOARD_BACKLIGHT_H

/**
 * If GPIO_EN_KEYBOARD_BACKLIGHT is defined, this GPIO will be set when
 * the the keyboard backlight is enabled or disabled. This GPIO is used
 * to enable or disable the power to the keyboard backlight circuitry.
 * GPIO_EN_KEYBOARD_BACKLIGHT must be active high.
 */

struct kblight_conf {
	const struct kblight_drv *drv;
};

struct kblight_drv {
	/**
	 * Initialize the keyboard backlight controller
	 * @return EC_SUCCESS or EC_ERROR_*
	 */
	int (*init)(void);

	/**
	 * Set the brightness
	 * @param percent
	 * @return EC_SUCCESS or EC_ERROR_*
	 */
	int (*set)(int percent);

	/**
	 * Get the current brightness
	 * @return Brightness in percentage
	 */
	int (*get)(void);

	/**
	 * Enable or disable keyboard backlight
	 * @param enable: 1=Enable, 0=Disable.
	 * @return EC_SUCCESS or EC_ERROR_*
	 */
	int (*enable)(int enable);

	/**
	 * Get the enabled state.
	 * @return 1=Enable, 0=Disable, -1=Failed to read enabled state.
	 */
	int (*get_enabled)(void);
};

/**
 * Initialize keyboard backlight per board
 */
void board_kblight_init(void);

/**
 * Set keyboard backlight brightness
 *
 * @param percent Brightness in percentage
 * @return EC_SUCCESS or EC_ERROR_*
 */
int kblight_set(int percent);

/**
 * Get keyboard backlight brightness
 *
 * @return Brightness in percentage
 */
int kblight_get(void);

/**
 * Enable or disable keyboard backlight
 *
 * @param enable: 1=Enable, 0=Disable.
 * @return EC_SUCCESS or EC_ERROR_*
 */
int kblight_enable(int enable);

/**
 * Register keyboard backlight controller
 *
 * @param drv: Driver of keyboard backlight controller
 * @return EC_SUCCESS or EC_ERROR_*
 */
int kblight_register(const struct kblight_drv *drv);

extern const struct kblight_drv kblight_pwm;

#endif /* __CROS_EC_KEYBOARD_BACKLIGHT_H */
