/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"
#include "board_host_command.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "input_module.h"
#include "task.h"
#include "util.h"
#include "zephyr_console_shim.h"
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "board_adc.h"
#include "flash_storage.h"
#include "lid_switch.h"
#include "hid_device.h"

LOG_MODULE_REGISTER(inputmodule, LOG_LEVEL_INF);
#define INPUT_MODULE_POLL_INTERVAL 10*MSEC
#define INPUT_MODULE_POWER_ON_DELAY (300*MSEC)
#define INPUT_MODULE_MUX_DELAY_US 2

int oc_count;
int force_on;
int detect_mode;
int hub_board_id[8];	/* EC console Debug use */
enum input_deck_state deck_state;

void module_oc_interrupt(enum gpio_signal signal)
{
    oc_count++;
}

void set_detect_mode(int mode)
{
	detect_mode = mode;
}

int get_detect_mode(void)
{
	return detect_mode;
}


static void set_hub_mux(uint8_t input)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a0),
		((input & BIT(0)) >> 0));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a1),
		((input & BIT(1)) >> 1));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a2),
		((input & BIT(2)) >> 2));
}

static void scan_c_deck(bool full_scan)
{
    int i;
	if (full_scan) {
		for (i = 0; i < 8; i++) {
			/* Switch the mux */
			set_hub_mux(i);
			/*
			 * In the specification table Switching Characteristics over Operating
			 * range the maximum Bus Select Time needs 6.6 ns, so delay a little
			 */
			usleep(INPUT_MODULE_MUX_DELAY_US);

			hub_board_id[i] = get_hardware_id(ADC_HUB_BOARD_ID);
		}
	} else {
		set_hub_mux(TOUCHPAD);
		usleep(INPUT_MODULE_MUX_DELAY_US);
		hub_board_id[TOUCHPAD] = get_hardware_id(ADC_HUB_BOARD_ID);
	}
	/* Turn off hub mux pins*/
	set_hub_mux(TOP_ROW_NOT_CONNECTED);
}

static void enable_touchpad_emulate(int enable)
{
	static int is_emulate = 1; /* will register the hid target in hid_device.c */
	int rv;

	/**
	 * TODO: if register/unregister fail, need to retry the register
	 */
	if (is_emulate != enable) {
		if (enable) {
			rv = hid_target_register(DEVICE_DT_GET(DT_NODELABEL(i2chid2)));
			LOG_INF("hid target register:%d", rv);
		} else {
			rv = hid_target_unregister(DEVICE_DT_GET(DT_NODELABEL(i2chid2)));
			LOG_INF("hid target unregister:%d", rv);
		}
		is_emulate = enable;
	}
}

static void board_input_module_init(void)
{
	/* need to wait bios_function_init() to update detect mode */
	if (detect_mode == 0x02)
		deck_state = DECK_FORCE_ON;
	else if (detect_mode == 0x04)
		deck_state = DECK_FORCE_OFF;
	else {
		deck_state = DECK_OFF;
	}
}
DECLARE_HOOK(HOOK_INIT, board_input_module_init, HOOK_PRIO_DEFAULT + 2);


bool input_deck_is_fully_populated(void)
{
	int i;

	if (hub_board_id[HUBBOARD] == INPUT_MODULE_SHORT ||
		hub_board_id[HUBBOARD] == INPUT_MODULE_DISCONNECTED) {
		return false;
	}
	for (i = 0; i <= TOP_ROW_4;) {
		switch (hub_board_id[i]) {
		case INPUT_MODULE_FULL_WIDTH:
			i += 5;
			break;
		case INPUT_MODULE_GENERIC_A:
		case INPUT_MODULE_KEYBOARD_A:
			i += 3;
			break;
		case INPUT_MODULE_GENERIC_B:
		case INPUT_MODULE_KEYBOARD_B:
			i += 2;
			break;
		case INPUT_MODULE_GENERIC_C:
			i++;
			break;
		default:
			return false;
		}
	}

	if (hub_board_id[TOUCHPAD] != INPUT_MODULE_TOUCHPAD) {
		return false;
	}

	return true;
}

/* Make sure the inputdeck is sleeping when lid is closed */
static void inputdeck_lid_change(void)
{
	/* If suspend or off we don't want to turn on input module LEDs even if the lid is open */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	if (lid_is_open()) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sleep_l), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sleep_l), 0);
	}

}
DECLARE_HOOK(HOOK_LID_CHANGE, inputdeck_lid_change, HOOK_PRIO_DEFAULT);


static void poll_c_deck(void);
DECLARE_DEFERRED(poll_c_deck);
static void poll_c_deck(void)
{
	static int turning_on_count;
	static int current_adc_ch = 0;

	hub_board_id[current_adc_ch] = get_hardware_id(ADC_HUB_BOARD_ID);
	current_adc_ch = (current_adc_ch + 1) % 8;
	set_hub_mux(current_adc_ch);

	if (current_adc_ch != 0) {
		hook_call_deferred(&poll_c_deck_data, INPUT_MODULE_POLL_INTERVAL);
		return;
	}
	switch (deck_state) {
	case DECK_OFF:
		break;
	case DECK_DISCONNECTED:
		/* TODO only poll touchpad and currently connected B1/C1 modules
		 * if c deck state is ON as these must be removed first
		 */
		if (input_deck_is_fully_populated()) {
			turning_on_count = 0;
			deck_state = DECK_TURNING_ON;
		} else {
			enable_touchpad_emulate(1);
		}
		break;
	case DECK_TURNING_ON:
		turning_on_count++;
		if (input_deck_is_fully_populated() &&
			turning_on_count > (INPUT_MODULE_POWER_ON_DELAY / (INPUT_MODULE_POLL_INTERVAL*8))) {
			enable_touchpad_emulate(0);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
			deck_state = DECK_ON;
			LOG_INF("Input modules on");
		} else if (hub_board_id[TOUCHPAD] != INPUT_MODULE_TOUCHPAD) {
			enable_touchpad_emulate(1);
			deck_state = DECK_DISCONNECTED;
		}
		break;
	case DECK_ON:
		/* TODO Add lid detection,
		 * if lid is closed input modules cannot be removed
		 */
		if (!input_deck_is_fully_populated()) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
			/* enable TP emulation */
			enable_touchpad_emulate(1);
			deck_state = DECK_DISCONNECTED;
			LOG_INF("Input modules off");
		}
		break;
	case DECK_FORCE_ON:
		enable_touchpad_emulate(0);
		break;
	case DECK_FORCE_OFF:
		enable_touchpad_emulate(1);
		break;
	default:
		break;
	}

	hook_call_deferred(&poll_c_deck_data, INPUT_MODULE_POLL_INTERVAL);
}

static void input_modules_powerup(void)
{
	if (deck_state == DECK_FORCE_ON)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
	else if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_OFF)
		deck_state = DECK_DISCONNECTED;

	hook_call_deferred(&poll_c_deck_data, INPUT_MODULE_POLL_INTERVAL);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, input_modules_powerup, HOOK_PRIO_DEFAULT);

void input_modules_reset(void)
{
	input_modules_powerdown();
	input_modules_powerup();
}

void input_modules_powerdown(void)
{
	if (deck_state == DECK_FORCE_ON)
		 gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
	else if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_OFF) {
		deck_state = DECK_OFF;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
		/* Hub mux input 6 is NC, so lower power draw  by disconnecting all PD */
		set_hub_mux(TOP_ROW_NOT_CONNECTED);
	}

	hook_call_deferred(&poll_c_deck_data, -1);
}

int get_deck_state(void)
{
	return deck_state;
}

/* Host command */
static enum ec_status check_deck_state(struct host_cmd_handler_args *args)
{
	const struct ec_params_deck_state *p = args->params;
	struct ec_response_deck_state *r = args->response;
	int idx;

	if ((get_detect_mode() != p->mode) && (p->mode != 0x00)) {
		/* set mode */
		if (p->mode == 0x01) {
			deck_state = DECK_DISCONNECTED;
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
		} else if (p->mode == 0x02) {
			deck_state = DECK_FORCE_ON;
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
		} else if (p->mode == 0x04) {
			deck_state = DECK_FORCE_OFF;
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
		}

		set_detect_mode(p->mode);
	}

	/* return deck status */
	for (idx = 0; idx < 8; idx++)
		r->input_deck_board_id[idx] = (uint8_t)hub_board_id[idx];

	r->deck_state = deck_state;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHECK_DECK_STATE, check_deck_state, EC_VER_MASK(0));

/* EC console command */
static int inputdeck_cmd(int argc, const char **argv)
{
	int i, mv, id;
	static const char * const deck_states[] = {
		"OFF", "DISCONNECTED", "TURNING_ON", "ON", "FORCE_OFF", "FORCE_ON", "NO_DETECTION"
	};

	if (argc >= 2) {
		if (!strncmp(argv[1], "on", 2)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
			ccprintf("Forcing Input modules on\n");
			deck_state = DECK_FORCE_ON;
		} else if (!strncmp(argv[1], "off", 3)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
			deck_state = DECK_FORCE_OFF;
		} else if (!strncmp(argv[1], "auto", 4)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
			deck_state = DECK_DISCONNECTED;
		} else if (!strncmp(argv[1], "nodetection", 4)) {
			deck_state = DECK_NO_DETECTION;
		}
	}
	scan_c_deck(true);
	ccprintf("Deck state: %s\n", deck_states[deck_state]);
	for (i = 0; i < 8; i++) {
			/* Switch the mux */
			set_hub_mux(i);
			/*
			 * In the specification table Switching Characteristics over Operating
			 * range the maximum Bus Select Time needs 6.6 ns, so delay a little
			 */
			usleep(INPUT_MODULE_MUX_DELAY_US);

			id = get_hardware_id(ADC_HUB_BOARD_ID);
			mv = adc_read_channel(ADC_HUB_BOARD_ID);
			ccprintf("    C-Deck status %d = %d %d mV", i, id, mv);
			switch (i) {
			case TOP_ROW_0:
				ccprintf(" [X - -    - -]");
				break;
			case TOP_ROW_1:
				ccprintf(" [- X -    - -]");
				break;
			case TOP_ROW_2:
				ccprintf(" [- - X    - -]");
				break;
			case TOP_ROW_3:
				ccprintf(" [- - -    X -]");
				break;
			case TOP_ROW_4:
				ccprintf(" [- - -    - X]");
				break;
			case TOUCHPAD:
				ccprintf(" [Touchpad    ]");
				break;
			case TOP_ROW_NOT_CONNECTED:
				ccprintf(" [Toprow disc.]");
				break;
			case HUBBOARD:
				ccprintf("  [Hubboard    ]");
				break;
			default:
				break;
			}
			switch (id) {
			case INPUT_MODULE_SHORT:
				ccprintf(" [Short]\n");
				break;
			case INPUT_MODULE_FULL_WIDTH:
				ccprintf(" [Generic Full Width]\n");
				break;
			case INPUT_MODULE_GENERIC_A:
				ccprintf(" [Generic A]\n");
				break;
			case INPUT_MODULE_GENERIC_B:
				ccprintf(" [Generic B]\n");
				break;
			case INPUT_MODULE_GENERIC_C:
				ccprintf(" [Generic C]\n");
				break;
			case INPUT_MODULE_KEYBOARD_B:
				ccprintf(" [Keyboard B]\n");
				break;
			case INPUT_MODULE_KEYBOARD_A:
				ccprintf(" [Keyboard A]\n");
				break;
			case INPUT_MODULE_DISCONNECTED:
				ccprintf(" [Disconnected]\n");
				break;
			case INPUT_MODULE_TOUCHPAD:
				ccprintf(" [Touchpad]\n");
				break;
			default:
				ccprintf(" [Reserved]\n");
				break;
			}
		}

	ccprintf("Input module Overcurrent Events: %d\n", oc_count);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(inputdeck, inputdeck_cmd, "[on/off/auto/nodetection]",
			"Input modules power sequence control");
