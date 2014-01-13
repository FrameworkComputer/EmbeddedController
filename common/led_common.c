/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for blinking LEDs.
 */

#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "util.h"

#define LED_AUTO_CONTROL_FLAG(id) (1 << (id))

static uint32_t led_auto_control_flags = ~0x00;

static int led_is_supported(enum ec_led_id led_id)
{
	int i;

	for (i = 0; i < supported_led_ids_count; i++)
		if (led_id == supported_led_ids[i])
			return 1;

	return 0;
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
	return (led_auto_control_flags & LED_AUTO_CONTROL_FLAG(led_id)) != 0;
}

static int led_command_control(struct host_cmd_handler_args *args)
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
	} else {
		if (led_set_brightness(p->led_id, p->brightness) != EC_SUCCESS)
			return EC_RES_INVALID_PARAM;
		led_auto_control(p->led_id, 0);
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LED_CONTROL, led_command_control, EC_VER_MASK(1));
