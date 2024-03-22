/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "common.h"
#include "console.h"
#include "drivers/cros_displight.h"
#include "ec_commands.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "pwm.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pwm_shim, LOG_LEVEL_ERR);

#define HAS_PWM_GENERIC_CHANNEL(compat)                         \
	DT_NODE_HAS_PROP(DT_COMPAT_GET_ANY_STATUS_OKAY(compat), \
			 generic_pwm_channel)

#define PWM_GENERIC_CHANNEL_ID(compat) \
	DT_PROP(DT_COMPAT_GET_ANY_STATUS_OKAY(compat), generic_pwm_channel)

#ifdef CONFIG_PWM_KBLIGHT
static bool pwm_is_kblight(int type, int index)
{
	if (type == EC_PWM_TYPE_KB_LIGHT)
		return true;

#if HAS_PWM_GENERIC_CHANNEL(cros_ec_kblight_pwm)
	if (type == EC_PWM_TYPE_GENERIC &&
	    index == PWM_GENERIC_CHANNEL_ID(cros_ec_kblight_pwm))
		return true;
#endif /* HAS_PWM_GENERIC_CHANNEL(cros_ec_kblight_pwm) */

	return false;
}
#endif /* CONFIG_PWM_KBLIGHT */

#ifdef CONFIG_PLATFORM_EC_PWM_DISPLIGHT
static bool pwm_is_displight(int type, int index)
{
	if (type == EC_PWM_TYPE_DISPLAY_LIGHT)
		return true;

#if HAS_PWM_GENERIC_CHANNEL(cros_ec_displight)
	if (type == EC_PWM_TYPE_GENERIC &&
	    index == PWM_GENERIC_CHANNEL_ID(cros_ec_displight))
		return true;
#endif /* HAS_PWM_GENERIC_CHANNEL(cros_ec_displight) */

	return false;
}
#endif /* CONFIG_PLATFORM_EC_PWM_DISPLIGHT */

static enum ec_status
host_command_pwm_set_duty(struct host_cmd_handler_args *args)
{
	__maybe_unused const struct ec_params_pwm_set_duty *p = args->params;

#ifdef CONFIG_PLATFORM_EC_PWM_KBLIGHT
	if (pwm_is_kblight(p->pwm_type, p->index)) {
		kblight_set(PWM_RAW_TO_PERCENT(p->duty));
		kblight_enable(p->duty > 0);
		return EC_RES_SUCCESS;
	}
#endif
#ifdef CONFIG_PLATFORM_EC_PWM_DISPLIGHT
	if (pwm_is_displight(p->pwm_type, p->index)) {
		displight_set(PWM_RAW_TO_PERCENT(p->duty));
		return EC_RES_SUCCESS;
	}
#endif

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_DUTY, host_command_pwm_set_duty,
		     EC_VER_MASK(0));

static enum ec_status
host_command_pwm_get_duty(struct host_cmd_handler_args *args)
{
	__maybe_unused const struct ec_params_pwm_get_duty *p = args->params;
	__maybe_unused struct ec_response_pwm_get_duty *r = args->response;

#ifdef CONFIG_PLATFORM_EC_PWM_KBLIGHT
	if (pwm_is_kblight(p->pwm_type, p->index)) {
		r->duty = PWM_PERCENT_TO_RAW(kblight_get());
		args->response_size = sizeof(*r);
		return EC_RES_SUCCESS;
	}
#endif
#ifdef CONFIG_PLATFORM_EC_PWM_DISPLIGHT
	if (pwm_is_displight(p->pwm_type, p->index)) {
		r->duty = PWM_PERCENT_TO_RAW(displight_get());
		args->response_size = sizeof(*r);
		return EC_RES_SUCCESS;
	}
#endif

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_DUTY, host_command_pwm_get_duty,
		     EC_VER_MASK(0));
