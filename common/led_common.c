/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for blinking LEDs.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "util.h"

#define LED_AUTO_CONTROL_FLAG(id) (1 << (id))

static uint32_t led_auto_control_flags = ~0x00;

__overridable int led_is_supported(enum ec_led_id led_id)
{
	int i;
	static int supported_leds = -1;

	if (supported_leds == -1) {
		supported_leds = 0;

		for (i = 0; i < supported_led_ids_count; i++)
			supported_leds |= (1 << supported_led_ids[i]);
	}

	return ((1 << (int)led_id) & supported_leds);
}

void led_auto_control(enum ec_led_id led_id, int enable)
{
	if (enable)
		led_auto_control_flags |= LED_AUTO_CONTROL_FLAG(led_id);
	else
		led_auto_control_flags &= ~LED_AUTO_CONTROL_FLAG(led_id);
}

int led_auto_control_is_enabled(enum ec_led_id led_id)
{
	if (!led_is_supported(led_id))
		return 0;

	return (led_auto_control_flags & LED_AUTO_CONTROL_FLAG(led_id)) != 0;
}

/* Empty functions cannot be verified in testing to not have had any
 * side-effects, remove from coverage.
 * LCOV_EXCL_START
 */
__attribute__((weak)) void board_led_auto_control(void)
{
	/*
	 * The projects have only power led won't change the led
	 * state immediately as the auto command is called for
	 * they only check the led state while the power state
	 * is changed.
	 */
}
/* LCOV_EXCL_STOP */

static enum ec_status led_command_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_led_control *p = args->params;
	struct ec_response_led_control *r = args->response;
	int i;

	args->response_size = sizeof(*r);
	memset(r->brightness_range, 0, sizeof(r->brightness_range));

	if (!led_is_supported(p->led_id))
		return EC_RES_INVALID_PARAM;

	led_get_brightness_range(p->led_id, r->brightness_range);
	if (p->flags & EC_LED_FLAGS_QUERY)
		return EC_RES_SUCCESS;

	for (i = 0; i < EC_LED_COLOR_COUNT; i++)
		if (r->brightness_range[i] == 0 && p->brightness[i] != 0)
			return EC_RES_INVALID_PARAM;

	if (p->flags & EC_LED_FLAGS_AUTO) {
		led_auto_control(p->led_id, 1);
		if (!IS_ENABLED(CONFIG_LED_ONOFF_STATES))
			board_led_auto_control();
	} else {
		if (led_set_brightness(p->led_id, p->brightness) != EC_SUCCESS)
			return EC_RES_INVALID_PARAM;
		led_auto_control(p->led_id, 0);
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LED_CONTROL, led_command_control, EC_VER_MASK(1));

#ifndef CONFIG_ZEPHYR
__attribute__((weak)) void led_control(enum ec_led_id led_id,
				       enum ec_led_state state)
{
	/*
	 * Default weak implementation that does not affect the state of
	 * LED. Boards can provide their own implementation.
	 */
}
#endif
