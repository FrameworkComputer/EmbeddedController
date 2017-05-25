/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power/Battery LED control for Eve
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "registers.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_PWM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

#define LED_TICKS_PER_BEAT 2
#define LED_BEATS_PER_PHASE 2
#define NUM_PHASE 2
#define DOUBLE_TAP_TICK_LEN (LED_TICKS_PER_BEAT * 8)

static int led_debug;
static int double_tap;
static int double_tap_tick_count;
static int led_pattern;
static int led_ticks;

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_LEFT_LED, EC_LED_ID_RIGHT_LED};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

/* List of LED colors used */
enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_BLUE,
	LED_WHITE,
	LED_RED_2_3,
	LED_RED_1_3,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

/* List of supported LED patterns */
enum led_pattern {
	SOLID_GREEN = 0,
	WHITE_GREEN,
	SOLID_WHITE,
	WHITE_RED,
	SOLID_RED,
	PULSE_RED_1,
	PULSE_RED_2,
	BLINK_RED,
	OFF,
	LED_NUM_PATTERNS,
};

enum led_side {
	LED_LEFT = 0,
	LED_RIGHT,
	LED_BOTH
};

/*
 * LED patterns are described as two phases. Each phase has an associated LED
 * color and length in beats. The length of each beat is defined by the macro
 * LED_TICKS_PER_BEAT.
 */
struct led_phase {
	uint8_t color[NUM_PHASE];
	uint8_t len[NUM_PHASE];
};

/*
 * Pattern table. The len field is beats per color. 0 for len indicates that a
 * particular pattern never changes from the first phase.
 */
static const struct led_phase pattern[LED_NUM_PATTERNS] = {
	{ {LED_GREEN, LED_GREEN}, {0, 0} },
	{ {LED_WHITE, LED_GREEN}, {2, 4} },
	{ {LED_WHITE, LED_WHITE}, {0, 0} },
	{ {LED_WHITE, LED_RED}, {2, 4} },
	{ {LED_RED, LED_RED}, {0, 0} },
	{ {LED_RED, LED_RED_2_3}, {4, 4} },
	{ {LED_RED, LED_RED_1_3}, {2, 4} },
	{ {LED_RED, LED_OFF}, {1, 6} },
	{ {LED_OFF, LED_OFF}, {0, 0} },
};

/*
 * Brightness vs. color, in the order of off, red, green and blue. Values are
 * for % on PWM duty cycle time.
 */
#define PWM_CHAN_PER_LED 3
static const uint8_t color_brightness[LED_COLOR_COUNT][PWM_CHAN_PER_LED] = {
	/* {Red, Green, Blue}, */
	[LED_OFF]   = {0, 0, 0},
	[LED_RED]   = {80,  0, 0},
	[LED_GREEN] = {0, 80, 0},
	[LED_BLUE] = {0, 0, 80},
	[LED_WHITE]  = {100, 100, 100},
	[LED_RED_2_3]  = {53, 0, 0},
	[LED_RED_1_3]  = {27, 0, 0},
};

/*
 * When a double tap event occurs, a LED pattern is displayed based on the
 * current battery charge level. The LED patterns used for double tap under low
 * battery conditions are same patterns displayed when the battery is not
 * charging. The table below shows what battery charge level displays which
 * pattern.
 */
struct range_map {
	uint8_t max;
	uint8_t pattern;
};

static const struct range_map pattern_tbl[] = {
	{2, BLINK_RED},
	{4, PULSE_RED_2},
	{10, PULSE_RED_1},
	{15, SOLID_RED},
	{30, WHITE_RED},
	{90, SOLID_WHITE},
	{98, WHITE_GREEN},
	{100, SOLID_GREEN},
};

/**
 * Set LED color
 *
 * @param color	Enumerated color value
 * @param side		Left LED, Right LED, or both LEDs
 */
static void set_color(enum led_color color, enum led_side side)
{
	int i;
	static uint8_t saved_duty[LED_BOTH][PWM_CHAN_PER_LED];

	/* Set color for left LED */
	if (side == LED_LEFT || side == LED_BOTH) {
		for (i = 0; i < PWM_CHAN_PER_LED; i++) {
			if (saved_duty[LED_LEFT][i] !=
			    color_brightness[color][i]) {
				pwm_set_duty(PWM_CH_LED_L_RED + i,
					     100 - color_brightness[color][i]);
				saved_duty[LED_LEFT][i] =
					color_brightness[color][i];
			}
		}
	}

	/* Set color for right LED */
	if (side == LED_RIGHT || side == LED_BOTH) {
		for (i = 0; i < PWM_CHAN_PER_LED; i++) {
			if (saved_duty[LED_RIGHT][i] !=
			    color_brightness[color][i]) {
				pwm_set_duty(PWM_CH_LED_R_RED + i,
					     100 - color_brightness[color][i]);
				saved_duty[LED_RIGHT][i] =
					color_brightness[color][i];
			}
		}
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_LEFT_LED:
		/* Set brightness for left LED */
		pwm_set_duty(PWM_CH_LED_L_RED,
			     100 - brightness[EC_LED_COLOR_RED]);
		pwm_set_duty(PWM_CH_LED_L_BLUE,
			     100 - brightness[EC_LED_COLOR_BLUE]);
		pwm_set_duty(PWM_CH_LED_L_GREEN,
			     100 - brightness[EC_LED_COLOR_GREEN]);
		break;
	case EC_LED_ID_RIGHT_LED:
		/* Set brightness for right LED */
		pwm_set_duty(PWM_CH_LED_R_RED,
			     100 - brightness[EC_LED_COLOR_RED]);
		pwm_set_duty(PWM_CH_LED_R_BLUE,
			     100 - brightness[EC_LED_COLOR_BLUE]);
		pwm_set_duty(PWM_CH_LED_R_GREEN,
			     100 - brightness[EC_LED_COLOR_GREEN]);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_register_double_tap(void)
{
	double_tap = 1;
}

static void led_manage_pattern(int side)
{
	int color;
	int phase;

	/* Determine pattern phase */
	phase = led_ticks < LED_TICKS_PER_BEAT * pattern[led_pattern].len[0] ?
		0 : 1;
	color = pattern[led_pattern].color[phase];
	/* Set color for the current phase */
	set_color(color, side);

	/*
	 * Update led_ticks. If the len field is 0, then the pattern
	 * being used is just one color so no need to increase the tick count.
	 */
	if (pattern[led_pattern].len[0])
		if (++led_ticks == LED_TICKS_PER_BEAT *
		    (pattern[led_pattern].len[0] +
		     pattern[led_pattern].len[1]))
			led_ticks = 0;

	/* If double tap display is active, decrement its counter */
	if (double_tap_tick_count)
		double_tap_tick_count--;
}

static void eve_led_set_power_battery(void)
{
	enum charge_state chg_state = charge_get_state();
	int side;
	int percent_chg;
	enum led_pattern pattern;
	int tap = 0;

	if (double_tap) {
		/* Clear double tap indication */
		if (!chipset_in_state(CHIPSET_STATE_ON))
			/* If not in S0, then set tap on */
			tap = 1;
		double_tap = 0;
	}
	/* Get active charge port which maps directly to left/right LED */
	side = charge_manager_get_active_charge_port();
	/* Ensure that side can be safely used as an index */
	if (side < 0 || side >= CONFIG_USB_PD_PORT_COUNT)
		side = LED_BOTH;

	/* Get percent charge */
	percent_chg = charge_get_percent();

	/*
	 * If a double tap update is underway, let that complete before allowing
	 * the pattern to change.
	 */
	if (!double_tap_tick_count) {
		if (chg_state == PWR_STATE_CHARGE_NEAR_FULL ||
		    ((chg_state == PWR_STATE_DISCHARGE_FULL)
		     && extpower_is_present())) {
			pattern = SOLID_GREEN;
		} else if (chg_state == PWR_STATE_CHARGE) {
			pattern = SOLID_WHITE;
		} else {
			int i;

			/*
			 * Not currently charging. Select the pattern based on
			 * the battery charge level. If there is no double tap
			 * event to process, then only the low battery patterns
			 * are relevant.
			 */
			for (i = 0; i < ARRAY_SIZE(pattern_tbl); i++) {
				if (percent_chg <= pattern_tbl[i].max) {
					pattern = pattern_tbl[i].pattern;
					break;
				}
			}
			/*
			 * The patterns used for double tap and for not charging
			 * state are the same for low battery cases. But, if
			 * battery charge is high enough to be above SOLID_RED,
			 * then only display LED pattern if double tap has
			 * occurred.
			 */
			if (tap == 0 && pattern <= WHITE_RED)
				pattern = OFF;
			else
				/* Start double tap LED sequence */
				double_tap_tick_count = DOUBLE_TAP_TICK_LEN;
		}

		/* If the LED pattern will change, then reset tick count and set
		 * new pattern.
		 */
		if (pattern != led_pattern) {
			led_ticks = 0;
			led_pattern = pattern;
		}
	}

	led_manage_pattern(side);
}

static void led_init(void)
{
	/*
	 * Enable PWMs and set to 0% duty cycle.  If they're disabled,
	 * seems to ground the pins instead of letting them float.
	 */
	/* Initialize PWM channels for left LED */
	pwm_enable(PWM_CH_LED_L_RED, 1);
	pwm_enable(PWM_CH_LED_L_GREEN, 1);
	pwm_enable(PWM_CH_LED_L_BLUE, 1);

	/* Initialize PWM channels for right LED */
	pwm_enable(PWM_CH_LED_R_RED, 1);
	pwm_enable(PWM_CH_LED_R_GREEN, 1);
	pwm_enable(PWM_CH_LED_R_BLUE, 1);

	set_color(LED_OFF, LED_BOTH);
	led_pattern = OFF;
	led_ticks = 0;
	double_tap_tick_count = 0;
}
/* After pwm_pin_init() */
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/**
 * Called by hook task every 250 ms
 */
static void led_tick(void)
{
	if (led_debug == 1)
		return;

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED) &&
	    led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
		eve_led_set_power_battery();
		return;
	}
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

/******************************************************************/
/* Console commands */
static int command_led(int argc, char **argv)
{
	int side = LED_BOTH;
	char *e;

	if (argc > 1) {
		if (argc > 2) {
			side = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM2;
			if (side > 1)
				return EC_ERROR_PARAM2;
		}

		if (!strcasecmp(argv[1], "debug")) {
			led_debug ^= 1;
			CPRINTF("led_debug = %d\n", led_debug);
		} else if (!strcasecmp(argv[1], "off")) {
			set_color(LED_OFF, side);
		} else if (!strcasecmp(argv[1], "red")) {
			set_color(LED_RED, side);
		} else if (!strcasecmp(argv[1], "green")) {
			set_color(LED_GREEN, side);
		} else if (!strcasecmp(argv[1], "blue")) {
			set_color(LED_BLUE, side);
		} else if (!strcasecmp(argv[1], "white")) {
			set_color(LED_WHITE, side);
		} else {
			/* maybe handle charger_discharge_on_ac() too? */
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led,
			"[debug|red|green|blue|white|amber|off <0|1>]",
			"Change LED color");
