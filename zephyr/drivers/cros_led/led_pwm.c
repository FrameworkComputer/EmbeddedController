/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <init.h>
#include <drivers/pwm.h>

#include "led_common.h"

#include "led.h"
#include "led_pwm.h"

#include <logging/log.h>

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM)

LOG_MODULE_DECLARE(led, LOG_LEVEL_ERR);

/*
 * LED driver that controls LEDs via PWM hardware, defined
 * via the cros_ec_multi_pwm_leds compatible.
 */

/*
 * Defines a single PWM output driving a LED.
 * These are used as an array representing
 * multiple PWM outputs to a grouped multi-colour LED.
 */
struct pwm_led {
	const struct device *pwm;
	uint8_t chan;
	pwm_flags_t flags;
};

/*
 * Multi-PWM LED driver read-only configuration.
 * Stores the DTS originated configuration for each
 * driver instance for the multi-PWM LED driver.
 */
struct led_multi_pwm {
	uint32_t period_us;	/* Period in microseconds */
	uint8_t pwm_count;	/* Number of PWMs */
	const struct pwm_led *pwms;
	const uint8_t *color_map;
};

#define LED_PWM_COLOR_MAP(id)	DT_CAT(L_C_M_, id)
#define LED_PWM_SPECS(id)       DT_CAT(L_P_S_, id)

/*
 * Generate the PWM list for each driver instance.
 * This list captures the PWM device, channel and flags for
 * each PWM used to drive the LEDs.
 * The list is named using a unique name based on the node name
 * so that it can be referenced later.
 */
#define GEN_PWM_ENTRY(id, p, idx)				\
{								\
	.pwm = DEVICE_DT_GET(DT_PWMS_CTLR_BY_IDX(id, idx)),	\
	.chan = DT_PWMS_CHANNEL_BY_IDX(id, idx),		\
	.flags = DT_PWMS_FLAGS_BY_IDX(id, idx),			\
},

#define GEN_PWM_TABLE(id)					\
static const struct pwm_led LED_PWM_SPECS(id)[] = {		\
	DT_FOREACH_PROP_ELEM(id, pwms, GEN_PWM_ENTRY)		\
};

DT_FOREACH_STATUS_OKAY(COMPAT_PWM, GEN_PWM_TABLE)

/*
 * Generate byte array holding color map for
 * EC host command brightness mapping.
 */
#define GEN_PWM_COLOR_MAP(id)					\
static const uint8_t LED_PWM_COLOR_MAP(id)[] = DT_PROP(id, color_list);

DT_FOREACH_STATUS_OKAY(COMPAT_PWM, GEN_PWM_COLOR_MAP)

/*
 * Generate driver structures. These represent the
 * top level multi-PWM configuration, and contains
 * references to the LED actions and the list
 * of PWMs for each driver instance.
 */
#define GEN_PWMS_TABLE(id)					\
{								\
	.period_us = USEC_PER_SEC / DT_PROP(id, frequency),	\
	.pwm_count = DT_PROP_LEN(id, pwms),			\
	.pwms = LED_PWM_SPECS(id),				\
	.color_map = LED_PWM_COLOR_MAP(id),			\
},

static const struct led_multi_pwm pwm_driver[] = {
DT_FOREACH_STATUS_OKAY(COMPAT_PWM, GEN_PWMS_TABLE)
};

/*
 * Update the LED PWM settings using the RGB color values provided.
 * The color values are defined as a percentage (0-100), and
 * this is used to calculate the duty cycle of the PWM.
 */
static void set_pwm_led_values(const struct led_multi_pwm *pwm,
			       const uint8_t *colors)
{
	const struct pwm_led *pl = pwm->pwms;

	for (int i = 0; i < pwm->pwm_count; i++, pl++, colors++) {
		/*
		 * Calculate PWM percentage duty cycle.
		 */
		uint32_t pulse = *colors * pwm->period_us / 100;

		pwm_pin_set_usec(pl->pwm, pl->chan, pwm->period_us,
				 pulse, pl->flags);
	}
}

void pwm_get_led_brightness_max(enum led_pwm_driver h, uint8_t *br)
{
	const struct led_multi_pwm *pwm = &pwm_driver[h];
	const uint8_t *cmp = pwm->color_map;
	/*
	 * Walk through the color_map for the PWM LED and
	 * set the brightness range to 255 for each
	 * of the supported colors.
	 */
	for (int i = 0; i < pwm->pwm_count; i++) {
		br[cmp[i]] = 255;
	}
}

void pwm_set_led_brightness(enum led_pwm_driver h, const uint8_t *br)
{
	uint8_t colors[4];
	const struct led_multi_pwm *pwm = &pwm_driver[h];
	const uint8_t *cmp = pwm->color_map;
	/*
	 * Walk through the color_map for the PWM LED and
	 * set the channel according to the
	 * brightness range selected. The color_map
	 * entries are the indices of the supported colors
	 * in the brightness array. The range for
	 * the brightness is 0-255, and this is converted
	 * to 0-100.
	 */
	for (int i = 0; i < pwm->pwm_count; i++) {
		colors[i] = ((int)br[cmp[i]] * 100) / 255;
	}
	set_pwm_led_values(pwm, colors);
}

/*
 * Set the LEDs of this multi-PWM LED driver instance.
 * to the colors passed.
 */
void pwm_set_led_colors(enum led_pwm_driver h, const uint8_t *colors)
{
	set_pwm_led_values(&pwm_driver[h], colors);
}
#endif /* DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM) */
