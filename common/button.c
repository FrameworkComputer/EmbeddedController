/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Button module for Chrome EC */

#include "button.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "timer.h"
#include "util.h"

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

struct button_state_t {
	uint64_t debounce_time;
	int debounced_pressed;
};

static struct button_state_t __bss_slow state[CONFIG_BUTTON_COUNT];

static uint64_t __bss_slow next_deferred_time;

#ifdef CONFIG_CMD_BUTTON
static int siml_btn_presd;

static int simulated_button_pressed(void)
{
	static int button = 1;

	button = !button;
	return button;
}
#endif

/*
 * Whether a button is currently pressed.
 */
static int raw_button_pressed(const struct button_config *button)
{
	int raw_value =
#ifdef CONFIG_CMD_BUTTON
			siml_btn_presd ?
			simulated_button_pressed() :
#endif
			gpio_get_level(button->gpio);

	return button->flags & BUTTON_FLAG_ACTIVE_HIGH ?
				       raw_value : !raw_value;
}

/*
 * Button initialization.
 */
static void button_init(void)
{
	int i;

	CPRINTS("(re)initializing buttons and interrupts");
	next_deferred_time = 0;
	for (i = 0; i < CONFIG_BUTTON_COUNT; i++) {
		state[i].debounced_pressed = raw_button_pressed(&buttons[i]);
		state[i].debounce_time = 0;
		gpio_enable_interrupt(buttons[i].gpio);
	}
}
DECLARE_HOOK(HOOK_INIT, button_init, HOOK_PRIO_DEFAULT);

/*
 * Handle debounced button changing state.
 */

static void button_change_deferred(void);
DECLARE_DEFERRED(button_change_deferred);

static void button_change_deferred(void)
{
	int i;
	int new_pressed;
	uint64_t soonest_debounce_time = 0;
	uint64_t time_now = get_time().val;

	for (i = 0; i < CONFIG_BUTTON_COUNT; i++) {
		/* Skip this button if we are not waiting to debounce */
		if (state[i].debounce_time == 0)
			continue;

		if (state[i].debounce_time <= time_now) {
			/* Check if the state has changed */
			new_pressed = raw_button_pressed(&buttons[i]);
			if (state[i].debounced_pressed != new_pressed) {
				state[i].debounced_pressed = new_pressed;
				CPRINTS("Button '%s' was %s",
					buttons[i].name, new_pressed ?
					"pressed" : "released");
#if defined(HAS_TASK_KEYPROTO) || defined(CONFIG_KEYBOARD_PROTOCOL_MKBP)
				keyboard_update_button(buttons[i].type,
					new_pressed);
#endif
			}

			/* Clear the debounce time to stop checking it */
			state[i].debounce_time = 0;
		} else {
			/*
			 * Make sure the next deferred call happens on or before
			 * each button needs it.
			 */
			soonest_debounce_time = (soonest_debounce_time == 0) ?
				state[i].debounce_time :
				MIN(soonest_debounce_time,
				    state[i].debounce_time);
		}
	}

	if (soonest_debounce_time != 0) {
		next_deferred_time = soonest_debounce_time;
		hook_call_deferred(&button_change_deferred_data,
				   next_deferred_time - time_now);
	}
}

/*
 * Handle a button interrupt.
 */
void button_interrupt(enum gpio_signal signal)
{
	int i;
	uint64_t time_now = get_time().val;

	for (i = 0; i < CONFIG_BUTTON_COUNT; i++) {
		if (buttons[i].gpio != signal)
			continue;

		state[i].debounce_time = time_now + buttons[i].debounce_us;
		if (next_deferred_time <= time_now ||
		    next_deferred_time > state[i].debounce_time) {
			next_deferred_time = state[i].debounce_time;
			hook_call_deferred(&button_change_deferred_data,
					   next_deferred_time - time_now);
		}
		break;
	}
}

#ifdef CONFIG_CMD_BUTTON
static int button_present(enum keyboard_button_type type)
{
	int i;

	for (i = 0; i < CONFIG_BUTTON_COUNT; i++)
		if (buttons[i].type == type)
			break;

	return i;
}

static void button_interrupt_simulate(int button)
{
	button_interrupt(buttons[button].gpio);
	usleep(buttons[button].debounce_us >> 2);
	button_interrupt(buttons[button].gpio);
}

static int console_command_button(int argc, char **argv)
{
	int button;
	int press_ms = 50;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "vup"))
		button = button_present(KEYBOARD_BUTTON_VOLUME_UP);
	else if (!strcasecmp(argv[1], "vdown"))
		button = button_present(KEYBOARD_BUTTON_VOLUME_DOWN);
	else
		return EC_ERROR_PARAM1;

	if (button == CONFIG_BUTTON_COUNT)
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		press_ms = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	siml_btn_presd = 1;

	/* Press the button */
	button_interrupt_simulate(button);

	/* Hold the button */
	msleep(press_ms);

	/* Release the button */
	button_interrupt_simulate(button);

	/* Wait till button processing is finished */
	msleep(100);

	siml_btn_presd = 0;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(button, console_command_button,
			"vup|vdown msec",
			"Simulate button press");
#endif
