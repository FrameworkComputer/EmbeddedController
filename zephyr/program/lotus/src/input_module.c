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

int oc_count;

void module_oc_interrupt(enum gpio_signal signal)
{
    oc_count++;
}

enum input_deck_state {
    DECK_OFF,
    DECK_ON
} deck_state;

int hub_board_id[8];	/* EC console Debug use */

static void scan_c_deck(void)
{
    int i;
	for (i = 0; i < 8; i++) {

		/* Switch the mux */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a0),
			((i & BIT(0)) >> 0));
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a1),
			((i & BIT(1)) >> 1));
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a2),
			((i & BIT(2)) >> 2));

		/*
		 * In the specification table Switching Characteristics over Operating Range,
		 * the maximum Bus Select Time needs 6.6 ns, so delay 1 us to stable
		 */
		usleep(1);

		hub_board_id[i] = get_hardware_id(ADC_HUB_BOARD_ID);
		/* Ignore the not intstall id */
		if (hub_board_id[i] == BOARD_VERSION_15)
			continue;
	}
}
#ifdef CONFIG_PLATFORM_CDECK_POWER_CONTROL
static void poll_c_deck(void)
{
    /* TODO only poll touchpad and currently connected B1/C1 modules
     * if c deck state is ON as these must be removed first
     */


	/**
	 * The minimum combination: Generic A size(8) + Generic B size(9) + Touchpad(13)
	 * The maximum combination: Keyboard(12) + Generic C size(10) *2 + Touchpad(13)
	 */

    /* TODO need to integrate with power sequencing logic so this shuts off in S3-G3 */
	if (hub_board_id_sum >= 30 && hub_board_id_sum <= 45)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);

}
DECLARE_HOOK(HOOK_TICK, poll_c_deck, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_PLATFORM_CDECK_POWER_CONTROL */

/* EC console command */
static int inputdeck_status(int argc, const char **argv)
{
	int i;
    scan_c_deck();
	for (i = 0; i < 8; i++)
		ccprintf("    C deck status %d = %d\n", i, hub_board_id[i]);

    ccprintf("Input module Overcurrent Events: %d\n", oc_count);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(inputdeck, inputdeck_status, "",
			"Get Input modules status");
