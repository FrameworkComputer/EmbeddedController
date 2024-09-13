/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/init.h>
#include "gpio/gpio_int.h"
#include "chipset.h"
#include "console.h"
#include "diagnostics.h"
#include "gpio.h"
#include "hooks.h"
#include "input_module.h"
#include "zephyr_console_shim.h"
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "board_adc.h"

LOG_MODULE_REGISTER(inputmodule, LOG_LEVEL_INF);

#define INPUT_MODULE_POLL_INTERVAL (10*MSEC)

enum input_deck_state deck_state;

static void board_input_module_init(void)
{
	deck_state = DECK_OFF;
}
DECLARE_HOOK(HOOK_INIT, board_input_module_init, HOOK_PRIO_DEFAULT + 2);

bool input_c_deck_detect(void)
{
	int touchpad;

	if (get_standalone_mode())
		return true;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_board_id_pug), 1);

		touchpad = get_hardware_id(ADC_TOUCHPAD_ID);

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_board_id_pug), 0);
		/*
		 * Q1 NMOS turn on,C deck connect is 990mV,disconnect is 1815 mv
		 */
		if (touchpad > BOARD_VERSION_10)
			return false;
	} else {
		touchpad = get_hardware_id(ADC_TOUCHPAD_ID);
		/*
		 * System power on, don't need to turn on the Q1 NMOS.
		 * The voltage is 0 V when the c deck is disconnected.
		 */
		if (touchpad < BOARD_VERSION_1)
			return false;
	}
	return true;
}

static void poll_c_deck(void);
DECLARE_DEFERRED(poll_c_deck);
static void poll_c_deck(void)
{
	switch (deck_state) {
	case DECK_OFF:
		break;
	case DECK_DISCONNECTED:
		if (input_c_deck_detect()) {
			deck_state = DECK_TURNING_ON;
		}
		break;
	case DECK_TURNING_ON:
		if (input_c_deck_detect()) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 1);
			deck_state = DECK_ON;
			LOG_INF("Input modules on");
		} else {
			deck_state = DECK_DISCONNECTED;
		}
		break;
	case DECK_ON:
		if (!input_c_deck_detect()) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 0);
			deck_state = DECK_DISCONNECTED;
			LOG_INF("Input modules off");
		}
		break;
	case DECK_FORCE_ON:
	case DECK_FORCE_OFF:
		break;
	default:
		break;
	}

	hook_call_deferred(&poll_c_deck_data, INPUT_MODULE_POLL_INTERVAL);
}

static void input_c_deck_powerup(void)
{
	if (deck_state == DECK_FORCE_ON)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 1);
	else if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_OFF)
		deck_state = DECK_DISCONNECTED;

	hook_call_deferred(&poll_c_deck_data, INPUT_MODULE_POLL_INTERVAL);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, input_c_deck_powerup, HOOK_PRIO_DEFAULT);

void input_c_deck_powerdown(void)
{
	if (deck_state == DECK_FORCE_ON)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 0);
	else if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_OFF) {
		deck_state = DECK_OFF;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 0);
	}

	hook_call_deferred(&poll_c_deck_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, input_c_deck_powerdown, HOOK_PRIO_DEFAULT);

/* EC console command */
static int inputdeck_cmd(int argc, const char **argv)
{
	static const char * const deck_states[] = {
		"OFF", "DISCONNECTED", "TURNING_ON", "ON", "FORCE_OFF", "FORCE_ON", "NO_DETECTION"
	};

	if (argc >= 2) {
		if (!strncmp(argv[1], "on", 2)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 1);
			ccprintf("Forcing Input modules on\n");
			deck_state = DECK_FORCE_ON;
		} else if (!strncmp(argv[1], "off", 3)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 0);
			deck_state = DECK_FORCE_OFF;
		} else if (!strncmp(argv[1], "auto", 4)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 0);
			deck_state = DECK_DISCONNECTED;
		} else if (!strncmp(argv[1], "nodetection", 4)) {
			deck_state = DECK_NO_DETECTION;
		}
	}

	ccprintf("Deck state: %s\n", deck_states[deck_state]);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(inputdeck, inputdeck_cmd, "[on/off/auto/nodetection]",
			"Input modules power sequence control");
