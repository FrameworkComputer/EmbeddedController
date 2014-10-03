/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Basic Chrome OS fan control */

#include "common.h"
#include "console.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "system.h"
#include "util.h"

/* True if we're listening to the thermal control task. False if we're setting
 * things manually. */
static int thermal_control_enabled[CONFIG_FANS];

#ifndef CONFIG_FAN_RPM_CUSTOM
/* This is the default implementation. It's only called over [0,100].
 * Convert the percentage to a target RPM. We can't simply scale all
 * the way down to zero because most fans won't turn that slowly, so
 * we'll map [1,100] => [FAN_MIN,FAN_MAX], and [0] => "off".
*/
int fan_percent_to_rpm(int fan, int pct)
{
	int rpm, max, min;

	if (!pct) {
		rpm = 0;
	} else {
		min = fans[fan].rpm_min;
		max = fans[fan].rpm_max;
		rpm = ((pct - 1) * max + (100 - pct) * min) / 99;
	}

	return rpm;
}
#endif	/* CONFIG_FAN_RPM_CUSTOM */

/* The thermal task will only call this function with pct in [0,100]. */
test_mockable void fan_set_percent_needed(int fan, int pct)
{
	int rpm;

	if (!thermal_control_enabled[fan])
		return;

	rpm = fan_percent_to_rpm(fan, pct);

	fan_set_rpm_target(fans[fan].ch, rpm);
}

static void set_enabled(int fan, int enable)
{
	fan_set_enabled(fans[fan].ch, enable);

	if (fans[fan].enable_gpio >= 0)
		gpio_set_level(fans[fan].enable_gpio, enable);
}

static void set_thermal_control_enabled(int fan, int enable)
{
	thermal_control_enabled[fan] = enable;

	/* If controlling the fan, need it in RPM-control mode */
	if (enable)
		fan_set_rpm_mode(fans[fan].ch, 1);
}

static void set_duty_cycle(int fan, int percent)
{
	/* Move the fan to manual control */
	fan_set_rpm_mode(fans[fan].ch, 0);

	/* Always enable the fan */
	set_enabled(fan, 1);

	/* Disable thermal engine automatic fan control. */
	set_thermal_control_enabled(fan, 0);

	/* Set the duty cycle */
	fan_set_duty(fans[fan].ch, percent);
}

/*****************************************************************************/
/* Console commands */

static int cc_fanauto(int argc, char **argv)
{
	char *e;
	int fan = 0;

	if (CONFIG_FANS > 1) {
		if (argc < 2) {
			ccprintf("fan number is required as the first arg\n");
			return EC_ERROR_PARAM_COUNT;
		}
		fan = strtoi(argv[1], &e, 0);
		if (*e || fan >= CONFIG_FANS)
			return EC_ERROR_PARAM1;
		argc--;
		argv++;
	}

	set_thermal_control_enabled(fan, 1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanauto, cc_fanauto,
			"{fan}",
			"Enable thermal fan control",
			NULL);

/* Return 0 for off, 1 for on, -1 for unknown */
static int is_powered(int fan)
{
	int is_pgood = -1;

	/* If we have an enable output, see if it's on or off. */
	if (fans[fan].enable_gpio >= 0)
		is_pgood = gpio_get_level(fans[fan].enable_gpio);
	/* If we have a pgood input, it overrides any enable output. */
	if (fans[fan].pgood_gpio >= 0)
		is_pgood = gpio_get_level(fans[fan].pgood_gpio);

	return is_pgood;
}

static int cc_faninfo(int argc, char **argv)
{
	static const char * const human_status[] = {
		"not spinning", "changing", "locked", "frustrated"
	};
	int tmp, is_pgood;
	int fan;
	char leader[20] = "";
	for (fan = 0; fan < CONFIG_FANS; fan++) {
		if (CONFIG_FANS > 1)
			snprintf(leader, sizeof(leader), "Fan %d ", fan);
		if (fan)
			ccprintf("\n");
		ccprintf("%sActual: %4d rpm\n", leader,
			 fan_get_rpm_actual(fans[fan].ch));
		ccprintf("%sTarget: %4d rpm\n", leader,
			 fan_get_rpm_target(fans[fan].ch));
		ccprintf("%sDuty:   %d%%\n", leader,
			 fan_get_duty(fans[fan].ch));
		tmp = fan_get_status(fans[fan].ch);
		ccprintf("%sStatus: %d (%s)\n", leader,
			 tmp, human_status[tmp]);
		ccprintf("%sMode:   %s\n", leader,
			 fan_get_rpm_mode(fans[fan].ch) ? "rpm" : "duty");
		ccprintf("%sAuto:   %s\n", leader,
			 thermal_control_enabled[fan] ? "yes" : "no");
		ccprintf("%sEnable: %s\n", leader,
			 fan_get_enabled(fans[fan].ch) ? "yes" : "no");
		is_pgood = is_powered(fan);
		if (is_pgood >= 0)
			ccprintf("%sPower:  %s\n", leader,
				 is_pgood ? "yes" : "no");
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(faninfo, cc_faninfo,
			NULL,
			"Print fan info",
			NULL);

static int cc_fanset(int argc, char **argv)
{
	int rpm;
	char *e;
	int fan = 0;

	if (CONFIG_FANS > 1) {
		if (argc < 2) {
			ccprintf("fan number is required as the first arg\n");
			return EC_ERROR_PARAM_COUNT;
		}
		fan = strtoi(argv[1], &e, 0);
		if (*e || fan >= CONFIG_FANS)
			return EC_ERROR_PARAM1;
		argc--;
		argv++;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	rpm = strtoi(argv[1], &e, 0);
	if (*e == '%') {		/* Wait, that's a percentage */
		ccprintf("Fan rpm given as %d%%\n", rpm);
		if (rpm < 0)
			rpm = 0;
		else if (rpm > 100)
			rpm = 100;
		rpm = fan_percent_to_rpm(fan, rpm);
	} else if (*e) {
		return EC_ERROR_PARAM1;
	}

	/* Move the fan to automatic control */
	fan_set_rpm_mode(fans[fan].ch, 1);

	/* Always enable the fan */
	set_enabled(fan, 1);

	/* Disable thermal engine automatic fan control. */
	set_thermal_control_enabled(fan, 0);

	fan_set_rpm_target(fans[fan].ch, rpm);

	ccprintf("Setting fan %d rpm target to %d\n", fan, rpm);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanset, cc_fanset,
			"{fan} (rpm | pct%)",
			"Set fan speed",
			NULL);

static int cc_fanduty(int argc, char **argv)
{
	int percent = 0;
	char *e;
	int fan = 0;

	if (CONFIG_FANS > 1) {
		if (argc < 2) {
			ccprintf("fan number is required as the first arg\n");
			return EC_ERROR_PARAM_COUNT;
		}
		fan = strtoi(argv[1], &e, 0);
		if (*e || fan >= CONFIG_FANS)
			return EC_ERROR_PARAM1;
		argc--;
		argv++;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	percent = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("Setting fan %d duty cycle to %d%%\n", fan, percent);
	set_duty_cycle(fan, percent);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanduty, cc_fanduty,
			"{fan} percent",
			"Set fan duty cycle",
			NULL);

/*****************************************************************************/
/* DPTF interface functions */

/* 0-100% if in duty mode. -1 if not */
int dptf_get_fan_duty_target(void)
{
	int fan = 0;				/* TODO(crosbug.com/p/23803) */

	if (thermal_control_enabled[fan] || fan_get_rpm_mode(fans[fan].ch))
		return -1;

	return fan_get_duty(fans[fan].ch);
}

/* 0-100% sets duty, out of range means let the EC drive */
void dptf_set_fan_duty_target(int pct)
{
	int fan;

	if (pct < 0 || pct > 100) {
		/* TODO(crosbug.com/p/23803) */
		for (fan = 0; fan < CONFIG_FANS; fan++)
			set_thermal_control_enabled(fan, 1);
	} else {
		/* TODO(crosbug.com/p/23803) */
		for (fan = 0; fan < CONFIG_FANS; fan++)
			set_duty_cycle(fan, pct);
	}
}

/*****************************************************************************/
/* Host commands */

static int hc_pwm_get_fan_target_rpm(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_fan_rpm *r = args->response;
	int fan = 0;

	/* TODO(crosbug.com/p/23803) */
	r->rpm = fan_get_rpm_target(fans[fan].ch);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_FAN_TARGET_RPM,
		     hc_pwm_get_fan_target_rpm,
		     EC_VER_MASK(0));

static int hc_pwm_set_fan_target_rpm(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_target_rpm_v1 *p_v1 = args->params;
	const struct ec_params_pwm_set_fan_target_rpm_v0 *p_v0 = args->params;
	int fan;

	if (args->version == 0) {
		for (fan = 0; fan < CONFIG_FANS; fan++) {
			/* Always enable the fan */
			set_enabled(fan, 1);

			set_thermal_control_enabled(fan, 0);
			fan_set_rpm_mode(fans[fan].ch, 1);
			fan_set_rpm_target(fans[fan].ch, p_v0->rpm);
		}

		return EC_RES_SUCCESS;
	}

	fan = p_v1->fan_idx;
	if (fan >= CONFIG_FANS)
		return EC_RES_ERROR;

	/* Always enable the fan */
	set_enabled(fan, 1);

	set_thermal_control_enabled(fan, 0);
	fan_set_rpm_mode(fans[fan].ch, 1);
	fan_set_rpm_target(fans[fan].ch, p_v1->rpm);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_TARGET_RPM,
		     hc_pwm_set_fan_target_rpm,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int hc_pwm_set_fan_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_duty_v1 *p_v1 = args->params;
	const struct ec_params_pwm_set_fan_duty_v0 *p_v0 = args->params;
	int fan;

	if (args->version == 0) {
		for (fan = 0; fan < CONFIG_FANS; fan++)
			set_duty_cycle(fan, p_v0->percent);

		return EC_RES_SUCCESS;
	}

	fan = p_v1->fan_idx;
	if (fan >= CONFIG_FANS)
		return EC_RES_ERROR;

	set_duty_cycle(fan, p_v1->percent);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_DUTY,
		     hc_pwm_set_fan_duty,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int hc_thermal_auto_fan_ctrl(struct host_cmd_handler_args *args)
{
	int fan;

	/* TODO(crosbug.com/p/23803) */
	for (fan = 0; fan < CONFIG_FANS; fan++)
		set_thermal_control_enabled(fan, 1);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_AUTO_FAN_CTRL,
		     hc_thermal_auto_fan_ctrl,
		     EC_VER_MASK(0));


/*****************************************************************************/
/* Hooks */

/* We only have a limited number of memory-mapped slots to report fan speed to
 * the AP. If we have more fans than that, some will be inaccessible. But
 * if we're using that many fans, we probably have bigger problems.
 */
BUILD_ASSERT(CONFIG_FANS <= EC_FAN_SPEED_ENTRIES);

#define PWMFAN_SYSJUMP_TAG 0x5046  /* "PF" */
#define PWM_HOOK_VERSION 1
/* Saved PWM state across sysjumps */
struct pwm_fan_state {
	/* TODO(crosbug.com/p/23530): Still treating all fans as one. */
	uint16_t fan_rpm;
	uint8_t fan_en;
};

static void pwm_fan_init(void)
{
	const struct pwm_fan_state *prev;
	uint16_t *mapped;
	int version, size;
	int i;
	int fan = 0;

	gpio_config_module(MODULE_PWM_FAN, 1);

	for (fan = 0; fan < CONFIG_FANS; fan++)
		fan_channel_setup(fans[fan].ch, fans[fan].flags);

	prev = (const struct pwm_fan_state *)
		system_get_jump_tag(PWMFAN_SYSJUMP_TAG, &version, &size);
	if (prev && version == PWM_HOOK_VERSION && size == sizeof(*prev)) {
		/* Restore previous state. */
		for (fan = 0; fan < CONFIG_FANS; fan++) {
			fan_set_enabled(fans[fan].ch, prev->fan_en);
			fan_set_rpm_target(fans[fan].ch, prev->fan_rpm);
		}
	} else {
		/* Set initial fan speed to maximum */
		for (fan = 0; fan < CONFIG_FANS; fan++)
			fan_set_rpm_target(fans[fan].ch, fans[fan].rpm_max);
	}

	for (fan = 0; fan < CONFIG_FANS; fan++)
		set_thermal_control_enabled(fan, 1);

	/* Initialize memory-mapped data */
	mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN);
	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++)
		mapped[i] = EC_FAN_SPEED_NOT_PRESENT;
}
DECLARE_HOOK(HOOK_INIT, pwm_fan_init, HOOK_PRIO_DEFAULT);

static void pwm_fan_second(void)
{
	uint16_t *mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN);
	int stalled = 0;
	int fan;

	for (fan = 0; fan < CONFIG_FANS; fan++) {
		if (fan_is_stalled(fans[fan].ch)) {
			mapped[fan] = EC_FAN_SPEED_STALLED;
			stalled = 1;
			cprints(CC_PWM, "Fan %d stalled!", fan);
		} else {
			mapped[fan] = fan_get_rpm_actual(fans[fan].ch);
		}
	}

	/*
	 * Issue warning.  As we have thermal shutdown
	 * protection, issuing warning here should be enough.
	 */
	if (stalled)
		host_set_single_event(EC_HOST_EVENT_THERMAL);
}
DECLARE_HOOK(HOOK_SECOND, pwm_fan_second, HOOK_PRIO_DEFAULT);

static void pwm_fan_preserve_state(void)
{
	struct pwm_fan_state state;
	int fan = 0;

	/* TODO(crosbug.com/p/23530): Still treating all fans as one. */
	state.fan_en = fan_get_enabled(fans[fan].ch);
	state.fan_rpm = fan_get_rpm_target(fans[fan].ch);

	system_add_jump_tag(PWMFAN_SYSJUMP_TAG, PWM_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, pwm_fan_preserve_state, HOOK_PRIO_DEFAULT);

static void pwm_fan_resume(void)
{
	int fan;
	for (fan = 0; fan < CONFIG_FANS; fan++)
		fan_set_enabled(fans[fan].ch, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pwm_fan_resume, HOOK_PRIO_DEFAULT);

static void pwm_fan_S3_S5(void)
{
	int fan;

	/* TODO(crosbug.com/p/23530): Still treating all fans as one. */
	for (fan = 0; fan < CONFIG_FANS; fan++) {
		/* Take back fan control when the processor shuts down */
		set_thermal_control_enabled(fan, 1);
		/* For now don't do anything with it. We'll have to turn it on
		 * again if we need active cooling during heavy battery
		 * charging or something.
		 */
		fan_set_rpm_target(fans[fan].ch, 0);
		fan_set_enabled(fans[fan].ch, 0); /* crosbug.com/p/8097 */
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pwm_fan_S3_S5, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pwm_fan_S3_S5, HOOK_PRIO_DEFAULT);
