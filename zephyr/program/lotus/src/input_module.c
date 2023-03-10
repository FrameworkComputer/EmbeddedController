/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "task.h"
#include "util.h"
#include "zephyr_console_shim.h"
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "board_adc.h"

LOG_MODULE_REGISTER(inputmodule, LOG_LEVEL_ERR);

#define INPUT_MODULE_POWER_ON_DELAY (40)
#define INPUT_MODULE_MUX_DELAY_US 10

int oc_count;
int force_on;

void module_oc_interrupt(enum gpio_signal signal)
{
    oc_count++;
}

enum input_deck_state {
    DECK_OFF,
	DECK_DISCONNECTED,
	DECK_TURNING_ON,
    DECK_ON,
	DECK_FORCE_OFF,
	DECK_FORCE_ON
} deck_state;

enum input_deck_mux {
	TOP_ROW_0 = 0,
	TOP_ROW_1,
	TOP_ROW_2,
	TOP_ROW_3,
	TOP_ROW_4,
	TOP_ROW_5,
	TOUCHPAD,
	HUBBOARD = 7
};

int hub_board_id[8];	/* EC console Debug use */

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
			* In the specification table Switching Characteristics over Operating Range,
			* the maximum Bus Select Time needs 6.6 ns, so delay 1 us to stable
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
	set_hub_mux(6);
}

#ifdef CONFIG_PLATFORM_CDECK_POWER_CONTROL
static void poll_c_deck(void)
{
	static int turning_on_count;

	switch (deck_state) {
		case DECK_OFF:
			break;
		case DECK_DISCONNECTED:
			scan_c_deck(true);
			/* TODO only poll touchpad and currently connected B1/C1 modules
			* if c deck state is ON as these must be removed first
			*/
			if (hub_board_id[TOUCHPAD] == 13) {
				turning_on_count = 0;
				deck_state = DECK_TURNING_ON;
			}
		break;
		case DECK_TURNING_ON:
			turning_on_count++;
			scan_c_deck(false);
			if (hub_board_id[TOUCHPAD] == 13 && turning_on_count > INPUT_MODULE_POWER_ON_DELAY) {
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
				deck_state = DECK_ON;
				CPRINTS("Input modules on");
			}
		break;
		case DECK_ON:
			/* TODO Add lid detection, if lid is closed input modules cannot be removed */

			scan_c_deck(false);
			if (hub_board_id[TOUCHPAD] > 14) {
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
				deck_state = DECK_DISCONNECTED;
				CPRINTS("Input modules off");
			}
			break;
		case DECK_FORCE_ON:
		case DECK_FORCE_OFF:
		default:
			break;
	}
}
DECLARE_HOOK(HOOK_TICK, poll_c_deck, HOOK_PRIO_DEFAULT);

static void input_modules_powerup(void)
{
	if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_ON) {
		deck_state = DECK_DISCONNECTED;
	}
}

DECLARE_HOOK(HOOK_CHIPSET_RESUME, input_modules_powerup, HOOK_PRIO_DEFAULT);
static void input_modules_powerdown(void)
{
	if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_ON) {
		deck_state = DECK_OFF;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
		/* Hub mux input 6 is NC, so lower power draw  by disconnecting all PD*/
		set_hub_mux(6);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, input_modules_powerdown, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, input_modules_powerdown, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_PLATFORM_CDECK_POWER_CONTROL */

/* EC console command */
static int inputdeck_cmd(int argc, const char **argv)
{
	int i;
	static const char * deck_states[] = {
		"OFF", "DISCONNECTED", "TURNING_ON", "ON", "FORCE_OFF", "FORCE_ON"
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
		}
	}
    scan_c_deck(true);
	ccprintf("Deck state: %s\n", deck_states[deck_state]);
	for (i = 0; i < 8; i++)
		ccprintf("    C deck status %d = %d\n", i, hub_board_id[i]);

    ccprintf("Input module Overcurrent Events: %d\n", oc_count);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(inputdeck, inputdeck_cmd, "[on/auto]",
			"Get Input modules status");
