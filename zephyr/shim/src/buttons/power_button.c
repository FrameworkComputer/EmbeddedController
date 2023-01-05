/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "host_command.h"
#include "power_button.h"
#include "task.h"
#include "util.h"

#include <zephyr/drivers/gpio_keys.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(power_button, CONFIG_GPIO_LOG_LEVEL);

#define POWER_BUTTON_LBL DT_NODELABEL(power_button)
#define POWER_BUTTON_IDX DT_NODE_CHILD_IDX(POWER_BUTTON_LBL)
#define GPIOKEYS_DEV DEVICE_DT_GET(DT_PARENT(POWER_BUTTON_LBL))

static struct power_button_data_s {
	int8_t state;
} power_button_data;

int power_button_is_pressed(void)
{
	return power_button_data.state;
}

int power_button_signal_asserted(void)
{
	return gpio_keys_get_pin(GPIOKEYS_DEV, POWER_BUTTON_IDX);
}

int power_button_wait_for_release(int timeout_us)
{
	int check_interval_us = 30000;
	bool released;

	released =
		WAIT_FOR(!(power_button_data.state), timeout_us,
			 task_wait_event(MIN(timeout_us, check_interval_us)));

	return released ? 0 : -ETIMEDOUT;
}

void handle_power_button(int8_t new_pin_state)
{
	LOG_DBG("Handling power button state=%d", new_pin_state);

	power_button_data.state = new_pin_state;

	hook_notify(HOOK_POWER_BUTTON_CHANGE);
	host_set_single_event(EC_HOST_EVENT_POWER_BUTTON);
}

void power_button_simulate_press(unsigned int duration)
{
	int8_t temp = power_button_data.state;

	LOG_INF("Simulating %d ms power button press.", duration);
	handle_power_button(1);

	if (duration > 0)
		k_sleep(K_MSEC(duration));

	LOG_INF("Simulating power button release.");
	handle_power_button(0);

	power_button_data.state = temp;
}

/* LCOV_EXCL_START */
/* Stubbed to be overridden by specific x86 board */
__overridable void power_button_pch_press(void)
{
}

/* Stubbed to be overridden by specific x86 board */
__overridable void power_button_pch_release(void)
{
}

/* Stubbed to be overridden by specific x86 board */
__overridable void power_button_pch_pulse(void)
{
}

/* Stubbed to be overridden by specific board */
__overridable int64_t get_time_dsw_pwrok(void)
{
	return 0;
}

/* Stubbed to be overridden by specific board */
__overridable void board_pwrbtn_to_pch(int level)
{
}
/* LCOV_EXCL_STOP */

/*****************************************************************************/
/* Console commands */
static int command_powerbtn(const struct shell *shell, size_t argc, char **argv)
{
	int ms = 200; /* Press duration in ms */
	char *e;

	if (argc > 1) {
		ms = strtoi(argv[1], &e, 0);
		if (*e || ms < 0)
			return EC_ERROR_PARAM1;
	}

	power_button_simulate_press(ms);
	return EC_SUCCESS;
}

SHELL_CMD_ARG_REGISTER(powerbtn, NULL,
		       "Simulate power button press for \'n\' msec",
		       command_powerbtn, 1, 1);
