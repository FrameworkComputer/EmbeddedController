/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_PWM_H
#define __CROS_EC_LED_PWM_H

#include "ec_commands.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/drivers/pwm.h>

#ifdef __cplusplus
extern "C" {
#endif
#endif

#ifdef CONFIG_ZEPHYR
#define PWM_LED_NO_CHANNEL NULL

struct pwm_led {
	const struct pwm_dt_spec *ch0;
	const struct pwm_dt_spec *ch1;
	const struct pwm_dt_spec *ch2;

	void (*set_duty)(const struct pwm_dt_spec *pwm, int percent);
};
#else
#define PWM_LED_NO_CHANNEL ((enum pwm_channel)(-1))

struct pwm_led {
	enum pwm_channel ch0;
	enum pwm_channel ch1;
	enum pwm_channel ch2;

	void (*enable)(enum pwm_channel ch, int enabled);
	void (*set_duty)(enum pwm_channel ch, int percent);
};
#endif

struct pwm_led_color_map {
	uint8_t ch0;
	uint8_t ch1;
	uint8_t ch2;
};

enum pwm_led_id {
	PWM_LED0 = 0,
#if CONFIG_LED_PWM_COUNT >= 2
	PWM_LED1,
#endif /* CONFIG_LED_PWM_COUNT > 2 */
};

/*
 * A mapping of color to LED duty cycles per channel.
 *
 * This should be defined by the boards to declare what each color looks like.
 * There should be an entry for every enum ec_led_colors value.  For colors that
 * are impossible for a given board, they should define a duty cycle of 0 for
 * all applicable channels.  (e.g. A bi-color LED which has a red and green
 * channel should define all 0s for EC_LED_COLOR_BLUE and EC_LED_COLOR_WHITE.)
 */
extern struct pwm_led_color_map led_color_map[EC_LED_COLOR_COUNT];

/*
 * A map of the PWM channels to logical PWM LEDs.
 *
 * A logical PWM LED would be considered as "per diffuser".  There may be 1-3
 * channels per diffuser and they should form a single entry in pwm_leds.  If a
 * channel is not used, simply define that channel as PWM_LED_NO_CHANNEL.
 */
extern struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT];

void set_pwm_led_color(enum pwm_led_id id, int color);

#ifdef __cplusplus
}
#endif

#endif /* defined(__CROS_EC_LED_PWM_H) */
