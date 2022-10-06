/* Copyright 2018 The ChromiumOS Authors
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
__override_proto void board_kblight_init(void);

/**
 * Shutdown keyboard backlight
 */
__override_proto void board_kblight_shutdown(void);

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

#ifdef TEST_BUILD
/**
 * @brief Get internal backlight enabled state. The value reported by
 *        kblight_get_enabled() can be outdated due to a deferred function call
 *        being required to update it. Using this function in tests improves
 *        reliability and reduces the need to sleep.
 *
 * @return uint8_t 0 if disabled, 1 otherwise.
 */
uint8_t kblight_get_current_enable(void);
#endif /* TEST_BUILD */

#endif /* __CROS_EC_KEYBOARD_BACKLIGHT_H */
