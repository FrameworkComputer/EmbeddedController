/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for blinking LEDs.
 */

#ifndef __CROS_EC_LED_COMMON_H
#define __CROS_EC_LED_COMMON_H

#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defined in led_<board>.c, not needed if led_is_supported is overridden */
extern const enum ec_led_id supported_led_ids[];

/* Defined in led_<board>.c, not needed if led_is_supported is overridden */
extern const int supported_led_ids_count;

/**
 * Whether an LED is supported.
 *
 * @param led_id	ID of LED to query.
 *
 * @returns		1 if LED is supported. 0 if it is not.
 *
 */
__override_proto int led_is_supported(enum ec_led_id led_id);

/**
 * Enable or disable automatic control of an LED.
 *
 * @param led_id	ID of LED to enable or disable automatic control.
 * @param enable	1 to enable . 0 to disable
 *
 */
void led_auto_control(enum ec_led_id led_id, int enable);

/**
 * Whether an LED is under automatic control.
 *
 * @param led_id	ID of LED to query.
 *
 * @returns		1 if LED is under automatic control. 0 if it is not.
 *
 */
int led_auto_control_is_enabled(enum ec_led_id led_id);

/**
 * Query brightness per color channel for an LED.
 *
 * @param led_id		ID of LED to query.
 * @param brightness_range	Points to EC_LED_COLOR_COUNT element array
 *				where current brightness will be stored.
 *	Value per color channel:
 *		0 unsupported,
 *		1 on/off control,
 *		2 -> 255 max brightness under PWM control.
 *
 */
void led_get_brightness_range(enum ec_led_id, uint8_t *brightness_range);

/**
 * Set brightness per color channel for an LED.
 *
 * @param led_id	ID of LED to set.
 * @param brightness	Brightness per color channel to set.
 *
 * @returns		EC_SUCCESS or EC_ERROR_INVAL
 *
 */
int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness);

/**
 * Enable LED.
 *
 * @param enable	1 to enable LED. 0 to disable.
 *
 */
void led_enable(int enable);

enum ec_led_state {
	LED_STATE_OFF = 0,
	LED_STATE_ON = 1,
	LED_STATE_RESET = 2,
};

/**
 * Check the LED state while receive the auto control
 * command.
 */
void board_led_auto_control(void);

/**
 * Control state of LED.
 *
 * @param led_id	ID of LED to control
 * @param state	0=off, 1=on, 2=reset to default
 *
 */
void led_control(enum ec_led_id id, enum ec_led_state state);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_LED_COMMON_H */
