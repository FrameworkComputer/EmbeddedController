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
#include "system.h"
#include "util.h"

/* HEY - this is temporary! (crosbug.com/p/23530) */
#define CONFIG_FAN_CH_CPU (fans[0].ch)
#define HEY0 0

/* True if we're listening to the thermal control task. False if we're setting
 * things manually. */
static int thermal_control_enabled;

#ifndef CONFIG_FAN_RPM_CUSTOM
/* This is the default implementation. It's only called over [0,100].
 * Convert the percentage to a target RPM. We can't simply scale all
 * the way down to zero because most fans won't turn that slowly, so
 * we'll map [1,100] => [FAN_MIN,FAN_MAX], and [0] => "off".
*/
int fan_percent_to_rpm(int pct)
{
	int rpm, max, min;

	if (!pct) {
		rpm = 0;
	} else {
		min = fans[HEY0].rpm_min;
		max = fans[HEY0].rpm_max;
		rpm = ((pct - 1) * max + (100 - pct) * min) / 99;
	}

	return rpm;
}
#endif	/* CONFIG_FAN_RPM_CUSTOM */

/* The thermal task will only call this function with pct in [0,100]. */
test_mockable void fan_set_percent_needed(int pct)
{
	int rpm;

	if (!thermal_control_enabled)
		return;

	rpm = fan_percent_to_rpm(pct);

	fan_set_rpm_target(CONFIG_FAN_CH_CPU, rpm);
}

static void set_enabled(int enable)
{
	fan_set_enabled(CONFIG_FAN_CH_CPU, enable);

	if (fans[HEY0].enable_gpio >= 0)
		gpio_set_level(fans[HEY0].enable_gpio, enable);
}

static void set_thermal_control_enabled(int enable)
{
	thermal_control_enabled	= enable;

	/* If controlling the fan, need it in RPM-control mode */
	if (enable)
		fan_set_rpm_mode(CONFIG_FAN_CH_CPU, 1);
}

static void set_duty_cycle(int percent)
{
	/* Move the fan to manual control */
	fan_set_rpm_mode(CONFIG_FAN_CH_CPU, 0);

	/* Always enable the fan */
	set_enabled(1);

	/* Disable thermal engine automatic fan control. */
	set_thermal_control_enabled(0);

	/* Set the duty cycle */
	fan_set_duty(CONFIG_FAN_CH_CPU, percent);
}

/*****************************************************************************/
/* Console commands */

static int cc_fanauto(int argc, char **argv)
{
	set_thermal_control_enabled(1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanauto, cc_fanauto,
			NULL,
			"Enable thermal fan control",
			NULL);

static int cc_faninfo(int argc, char **argv)
{
	static const char * const human_status[] = {
		"not spinning", "changing", "locked", "frustrated"
	};
	int tmp, is_pgood;

	ccprintf("Actual: %4d rpm\n",
		 fan_get_rpm_actual(CONFIG_FAN_CH_CPU));
	ccprintf("Target: %4d rpm\n",
		 fan_get_rpm_target(CONFIG_FAN_CH_CPU));
	ccprintf("Duty:   %d%%\n",
		 fan_get_duty(CONFIG_FAN_CH_CPU));
	tmp = fan_get_status(CONFIG_FAN_CH_CPU);
	ccprintf("Status: %d (%s)\n", tmp, human_status[tmp]);
	ccprintf("Mode:   %s\n",
		 fan_get_rpm_mode(CONFIG_FAN_CH_CPU) ? "rpm" : "duty");
	ccprintf("Auto:   %s\n", thermal_control_enabled ? "yes" : "no");
	ccprintf("Enable: %s\n",
		 fan_get_enabled(CONFIG_FAN_CH_CPU) ? "yes" : "no");

	/* Assume we don't know */
	is_pgood = -1;
	/* If we have an enable output, see if it's on or off. */
	if (fans[HEY0].enable_gpio >= 0)
		is_pgood = gpio_get_level(fans[HEY0].enable_gpio);
	/* If we have a pgood input, it overrides any enable output. */
	if (fans[HEY0].pgood_gpio >= 0)
		is_pgood = gpio_get_level(fans[HEY0].pgood_gpio);
	/* If we think we know, say so */
	if (is_pgood >= 0)
		ccprintf("Power:  %s\n", is_pgood ? "yes" : "no");

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

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	rpm = strtoi(argv[1], &e, 0);
	if (*e == '%') {		/* Wait, that's a percentage */
		ccprintf("Fan rpm given as %d%%\n", rpm);
		if (rpm < 0)
			rpm = 0;
		else if (rpm > 100)
			rpm = 100;
		rpm = fan_percent_to_rpm(rpm);
	} else if (*e) {
		return EC_ERROR_PARAM1;
	}

	/* Move the fan to automatic control */
	fan_set_rpm_mode(CONFIG_FAN_CH_CPU, 1);

	/* Always enable the fan */
	set_enabled(1);

	/* Disable thermal engine automatic fan control. */
	set_thermal_control_enabled(0);

	fan_set_rpm_target(CONFIG_FAN_CH_CPU, rpm);

	ccprintf("Setting fan rpm target to %d\n", rpm);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanset, cc_fanset,
			"rpm | pct%",
			"Set fan speed",
			NULL);

static int cc_fanduty(int argc, char **argv)
{
	int percent = 0;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	percent = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("Setting fan duty cycle to %d%%\n", percent);
	set_duty_cycle(percent);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanduty, cc_fanduty,
			"percent",
			"Set fan duty cycle",
			NULL);

/*****************************************************************************/
/* Host commands */

static int hc_pwm_get_fan_target_rpm(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_fan_rpm *r = args->response;

	r->rpm = fan_get_rpm_target(CONFIG_FAN_CH_CPU);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_FAN_TARGET_RPM,
		     hc_pwm_get_fan_target_rpm,
		     EC_VER_MASK(0));

static int hc_pwm_set_fan_target_rpm(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_target_rpm *p = args->params;

	set_thermal_control_enabled(0);
	fan_set_rpm_mode(CONFIG_FAN_CH_CPU, 1);
	fan_set_rpm_target(CONFIG_FAN_CH_CPU, p->rpm);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_TARGET_RPM,
		     hc_pwm_set_fan_target_rpm,
		     EC_VER_MASK(0));

static int hc_pwm_set_fan_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_duty *p = args->params;
	set_duty_cycle(p->percent);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_DUTY,
		     hc_pwm_set_fan_duty,
		     EC_VER_MASK(0));

static int hc_thermal_auto_fan_ctrl(struct host_cmd_handler_args *args)
{
	set_thermal_control_enabled(1);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_AUTO_FAN_CTRL,
		     hc_thermal_auto_fan_ctrl,
		     EC_VER_MASK(0));


/*****************************************************************************/
/* Hooks */

#define PWMFAN_SYSJUMP_TAG 0x5046  /* "PF" */
#define PWM_HOOK_VERSION 1
/* Saved PWM state across sysjumps */
struct pwm_fan_state {
	uint16_t fan_rpm;
	uint8_t fan_en;
	char pad; /* Pad to multiple of 4 bytes. */
};

static void pwm_fan_init(void)
{
	const struct pwm_fan_state *prev;
	uint16_t *mapped;
	int version, size;
	int i;

	gpio_config_module(MODULE_PWM_FAN, 1);

	for (i = 0; i < CONFIG_FANS; i++)
		fan_channel_setup(fans[i].ch, fans[i].flags);

	prev = (const struct pwm_fan_state *)
		system_get_jump_tag(PWMFAN_SYSJUMP_TAG, &version, &size);
	if (prev && version == PWM_HOOK_VERSION && size == sizeof(*prev)) {
		/* Restore previous state. */
		fan_set_enabled(CONFIG_FAN_CH_CPU, prev->fan_en);
		fan_set_rpm_target(CONFIG_FAN_CH_CPU, prev->fan_rpm);
	} else {
		/* Set initial fan speed to maximum */
		fan_set_duty(CONFIG_FAN_CH_CPU, 100);
	}

	set_thermal_control_enabled(1);

	/* Initialize memory-mapped data */
	mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN);
	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++)
		mapped[i] = EC_FAN_SPEED_NOT_PRESENT;
}
DECLARE_HOOK(HOOK_INIT, pwm_fan_init, HOOK_PRIO_DEFAULT + 1);

static void pwm_fan_second(void)
{
	uint16_t *mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN);

	if (fan_is_stalled(CONFIG_FAN_CH_CPU)) {
		mapped[0] = EC_FAN_SPEED_STALLED;
		/*
		 * Issue warning.  As we have thermal shutdown
		 * protection, issuing warning here should be enough.
		 */
		host_set_single_event(EC_HOST_EVENT_THERMAL);
		cprintf(CC_PWM, "[%T Fan stalled!]\n");
	} else {
		mapped[0] = fan_get_rpm_actual(CONFIG_FAN_CH_CPU);
	}
}
DECLARE_HOOK(HOOK_SECOND, pwm_fan_second, HOOK_PRIO_DEFAULT);

static void pwm_fan_preserve_state(void)
{
	struct pwm_fan_state state;

	state.fan_en = fan_get_enabled(CONFIG_FAN_CH_CPU);
	state.fan_rpm = fan_get_rpm_target(CONFIG_FAN_CH_CPU);

	system_add_jump_tag(PWMFAN_SYSJUMP_TAG, PWM_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, pwm_fan_preserve_state, HOOK_PRIO_DEFAULT);

static void pwm_fan_resume(void)
{
	fan_set_enabled(CONFIG_FAN_CH_CPU, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pwm_fan_resume, HOOK_PRIO_DEFAULT);

static void pwm_fan_S3_S5(void)
{
	/* Take back fan control when the processor shuts down */
	set_thermal_control_enabled(1);
	/* For now don't do anything with it. We'll have to turn it on again if
	 * we need active cooling during heavy battery charging or something.
	 */
	fan_set_rpm_target(CONFIG_FAN_CH_CPU, 0);
	fan_set_enabled(CONFIG_FAN_CH_CPU, 0); /* crosbug.com/p/8097 */
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pwm_fan_S3_S5, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pwm_fan_S3_S5, HOOK_PRIO_DEFAULT);
