/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_H__
#define __CROS_EC_LED_H__

#include "ec_commands.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

/*
 * Return string-token if the property exists, otherwise return 0
 */
#define GET_PROP(id, prop)                      \
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop), \
		    (DT_STRING_UPPER_TOKEN(id, prop)), (0))

#define PINS_NODE(id) DT_CAT(LEDPIN_, id)
#define PINS_ARRAY(id) DT_CAT(PINS_ARRAY_, id)
#define DATA_NODE(node_id) DT_CAT(DATA_NODE_, node_id)

/* Build-time bitmask of supported IDs for this driver */
#define LED_ID_BIT(node_id) | (1 << DT_STRING_UPPER_TOKEN(node_id, led_id))
#define GET_DRIVER_ID_MASK(inst) \
	(0 DT_FOREACH_CHILD(DT_DRV_INST(inst), LED_ID_BIT))

#define LED_ANIMATION_TICK_MS CONFIG_PLATFORM_EC_LED_ANIMATION_TICK_MS

enum led_color {
	LED_OFF,
	LED_RED,
	LED_GREEN,
	LED_BLUE,
	LED_YELLOW,
	LED_WHITE,
	LED_AMBER,
	LED_MAGENTA,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

/* EC_LED_COLOR maps to LED_COLOR - 1 */
BUILD_ASSERT((LED_RED - 1) == EC_LED_COLOR_RED);
BUILD_ASSERT((LED_GREEN - 1) == EC_LED_COLOR_GREEN);
BUILD_ASSERT((LED_BLUE - 1) == EC_LED_COLOR_BLUE);
BUILD_ASSERT((LED_YELLOW - 1) == EC_LED_COLOR_YELLOW);
BUILD_ASSERT((LED_WHITE - 1) == EC_LED_COLOR_WHITE);
BUILD_ASSERT((LED_AMBER - 1) == EC_LED_COLOR_AMBER);
BUILD_ASSERT((LED_MAGENTA - 1) == EC_LED_COLOR_MAGENTA);
BUILD_ASSERT((LED_COLOR_COUNT - 1) == EC_LED_COLOR_COUNT);

enum led_transition {
	LED_TRANSITION_STEP,
	LED_TRANSITION_LINEAR,
	LED_TRANSITION_EXPONENTIAL,

	LED_TRANSITION_COUNT
};

/*
 * Board specific override that allows the board to define its own alt
 * led policies at run time.
 *
 * @return>-int to represent the label of each board-led-alt-policy.
 */
__overridable int board_led_alt_policy(void);

#define LED_ENUM(id, enum_name) DT_STRING_TOKEN(id, enum_name)
#define LED_ENUM_WITH_COMMA(id, enum_name)           \
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name), \
		    (LED_ENUM(id, enum_name), ), ())

/*
 * Struct defining LED GPIO pin and value to set.
 */
struct gpio_pin_t {
	enum gpio_signal signal;
	int val;
};

/* current state of the pwm pin */
struct pwm_data_t {
	struct pwm_dt_spec pwm_spec;
	uint32_t pulse_ns;
	int32_t pulse_step_ns;
};

/*
 * Struct defining LED PWM pin and duty cycle to set.
 */
struct pwm_pin_t {
	struct pwm_data_t *pwm;
	/*
	 * PWM Duty cycle ns.
	 * Not strictly positive because intermediate values may be negative
	 * while calculating transitions. For example, in a linear transition
	 * from a brightness of 100 to 0, we will calculate the decrease in
	 * brightness as a negative transition and add the negative pulse_ns to
	 * the current pulse_ns to calculate the new pulse_ns.
	 */
	int32_t pulse_ns;
	int32_t pulse_step_ns;
};

/* Shared function table for LED driver implementations. */
struct led_driver_api {
	/**
	 * Set LED color using pattern node.
	 */
	void (*set_color_with_pattern)(void *pattern);

	/**
	 * Commit the calculated LED color/duty cycles to the hardware.
	 *
	 * This decouples the pattern logic from the physical application,
	 * allowing drivers to perform asynchronous updates or smooth
	 * transitions without re-evaluating the policy.
	 *
	 * @param has_transitions True if the active policy uses a transition
	 * pattern.
	 */

	void (*asynchronous_apply_color)(bool has_transitions);

	/**
	 * Set LED color using color enum
	 *
	 * @param color		LED Color to enable
	 * @param led_id	LED ID to set the color for
	 * @param brightness	Brightness to set the color to
	 */
	void (*set_color)(enum led_color color, enum ec_led_id led_id,
			  uint8_t brightness);

	/**
	 * Get the brightness range for each supported color channel.
	 *
	 * Used to query LED capabilities, such as which color channels
	 * are present and whether they support dimming.
	 *
	 * @param led_id            LED ID to query.
	 * @param brightness_range  Output array for brightness ranges.
	 */
	void (*get_brightness_range)(enum ec_led_id led_id,
				     uint8_t *brightness_range);

	/**
	 * Manually set the brightness for each color channel.
	 *
	 * @param led_id      LED ID to set brightness for.
	 * @param brightness  Array of brightness levels for each color.
	 * @return EC_SUCCESS on success, or an error status.
	 */
	int (*set_brightness)(enum ec_led_id led_id, const uint8_t *brightness);
};

/* Driver handle containing the API table */
struct led_driver_t {
	/* Bitmask of ec_led_id values supported by this driver */
	uint32_t led_id_mask;
	const struct led_driver_api *api;
};

/*
 * Pin node contains LED color and array of gpio/pwm pins
 * to alter in order to enable the given color.
 */
struct led_pins_node_t {
	/* 4-byte members first */
	/*
	 * Pointer to driver-specific pin configuration data used to
	 * enable a particular color. The underlying driver is responsible
	 * for casting this to the correct type.
	 */
	void *pins;

	/* 1-byte members following */
	/*
	 * The color ID this node represents. Only used to support
	 * ectool functionality.
	 */
	uint8_t led_color;

	/*
	 * The logical LED ID this node belongs to. Only used to support
	 * ectool functionality.
	 */
	enum ec_led_id led_id;

	/*
	 * 0-based devicetree child index of the color.
	 * Must be matched with led_id to resolve the actual pins_node.
	 */
	uint8_t color_idx;

	/* Number of pins per color */
	uint8_t pins_count;
} __packed;

struct pattern_color_node_t {
	uint16_t duration_ms;
	uint8_t color_idx;
} __packed;

struct led_pattern_node_t {
	/* 4-byte members first */
	uint32_t elapsed_ms;
	const struct pattern_color_node_t *pattern_color;

	/* 1-byte members following */
	enum ec_led_id led_id;
	uint8_t cur_color;
	uint8_t pattern_len;
	uint8_t cycle_limit;
	uint8_t cycle_curr;
	enum led_transition transition;
	bool needs_update;
} __packed;

static inline uint32_t get_step_duration(const struct led_pattern_node_t *cfg,
					 uint8_t step_idx)
{
	return cfg->pattern_color[step_idx].duration_ms;
}

/**
 * Wrapper function to set LED color.
 *
 * @param color			LED Color to enable
 * @param led_id		LED ID to set the color for
 * @param brightness	Brightness to set the color to
 */
void led_set_color(enum led_color color, enum ec_led_id led_id,
		   uint8_t brightness);

struct custom_led_patterns_t {
	struct led_pattern_node_t *led_patterns;
	uint8_t num_patterns;
	enum ec_led_id led_id;
};

/**
 * Sets or clears custom LED patterns to be executed by the LED task.
 *
 * Overrides default devicetree patterns for the specified led_id until
 * they complete their cycles.
 *
 * @param p Pointer to custom patterns. If NULL, custom patterns are cleared.
 *          The data pointed to must remain valid until execution completes.
 */
void led_set_custom_patterns(struct custom_led_patterns_t *p);

/* Reset any built-in patterns that match the given led_id */
void reset_policy_patterns(enum ec_led_id led_id);

#ifdef TEST_BUILD
const struct led_pins_node_t *led_get_node(enum led_color color,
					   enum ec_led_id led_id);

enum power_state get_chipset_state(void);
#endif /* TEST_BUILD */

#endif /* __CROS_EC_LED_H__ */
