/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for Chromebook fans */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*****************************************************************************/
/* Chip-specific stuff */

/* Maximum RPM for fan controller */
#define MAX_RPM 0x1fff
/* Max PWM for fan controller */
#define MAX_PWM 0x1ff
/*
 * Scaling factor for requested/actual RPM for CPU fan.  We need this because
 * the fan controller on Blizzard filters tach pulses that are less than 64
 * 15625Hz ticks apart, which works out to ~7000rpm on an unscaled fan.  By
 * telling the controller we actually have twice as many edges per revolution,
 * the controller can handle fans that actually go twice as fast.  See
 * crosbug.com/p/7718.
 */
#define CPU_FAN_SCALE 2

static int fan_get_enabled(void)
{
	return (LM4_FAN_FANCTL & (1 << FAN_CH_CPU)) ? 1 : 0;
}

static void fan_set_enabled(int enable)
{
	if (enable)
		LM4_FAN_FANCTL |= (1 << FAN_CH_CPU);
	else
		LM4_FAN_FANCTL &= ~(1 << FAN_CH_CPU);

#ifdef CONFIG_PWM_FAN_EN_GPIO
	gpio_set_level(CONFIG_PWM_FAN_EN_GPIO, enable);
#endif /* CONFIG_PWM_FAN_EN_GPIO */
}

static int fan_get_rpm_mode(void)
{
	return (LM4_FAN_FANCH(FAN_CH_CPU) & 0x0001) ? 0 : 1;
}

static void fan_set_rpm_mode(int rpm_mode)
{
	int was_enabled = fan_get_enabled();
	int was_rpm = fan_get_rpm_mode();

	if (!was_rpm && rpm_mode) {
		/* Enable RPM control */
		fan_set_enabled(0);
		LM4_FAN_FANCH(FAN_CH_CPU) &= ~0x0001;
		fan_set_enabled(was_enabled);
	} else if (was_rpm && !rpm_mode) {
		/* Disable RPM mode */
		fan_set_enabled(0);
		LM4_FAN_FANCH(FAN_CH_CPU) |= 0x0001;
		fan_set_enabled(was_enabled);
	}
}

static int fan_get_rpm_actual(void)
{
	return (LM4_FAN_FANCST(FAN_CH_CPU) & MAX_RPM) * CPU_FAN_SCALE;
}

static int fan_get_rpm_target(void)
{
	return (LM4_FAN_FANCMD(FAN_CH_CPU) & MAX_RPM) * CPU_FAN_SCALE;
}

static void fan_set_rpm_target(int rpm)
{
	/* Apply fan scaling */
	if (rpm > 0)
		rpm /= CPU_FAN_SCALE;

	/* Treat out-of-range requests as requests for maximum fan speed */
	if (rpm < 0 || rpm > MAX_RPM)
		rpm = MAX_RPM;

	LM4_FAN_FANCMD(FAN_CH_CPU) = rpm;
}

static int fan_get_duty_raw(void)
{
	return (LM4_FAN_FANCMD(FAN_CH_CPU) >> 16) & MAX_PWM;
}

static void fan_set_duty_raw(int pwm)
{
	LM4_FAN_FANCMD(FAN_CH_CPU) = pwm << 16;
}

static int fan_get_status(void)
{
	return (LM4_FAN_FANSTS >> (2 * FAN_CH_CPU)) & 0x03;
}
static const char * const human_status[] = {
	"not spinning", "changing", "locked", "frustrated"
};

/**
 * Return non-zero if fan is enabled but stalled.
 */
static int fan_is_stalled(void)
{
	/* Must be enabled with non-zero target to stall */
	if (!fan_get_enabled() || fan_get_rpm_target() == 0)
		return 0;

	/* Check for stall condition */
	return (((LM4_FAN_FANSTS >> (2 * FAN_CH_CPU)) & 0x03) == 0) ? 1 : 0;
}

/*****************************************************************************/
/* Control functions */

/* True if we're listening to the thermal control task. False if we're setting
 * things manually. */
static int thermal_control_enabled;

static void fan_set_thermal_control_enabled(int enable)
{
	thermal_control_enabled	= enable;

	/* If controlling the fan, need it in RPM-control mode */
	if (enable)
		fan_set_rpm_mode(1);
}

/* The thermal task will only call this function with pct in [0,100]. */
void pwm_fan_set_percent_needed(int pct)
{
	int rpm;

	if (!thermal_control_enabled)
		return;

	rpm = pwm_fan_percent_to_rpm(pct);

	fan_set_rpm_target(rpm);
}

static int fan_get_duty_cycle(void)
{
	return fan_get_duty_raw() * 100 / MAX_PWM;
}

static void fan_set_duty_cycle(int percent)
{
	int pwm;

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	pwm = (MAX_PWM * percent) / 100;

	/* Move the fan to manual control */
	fan_set_rpm_mode(0);

	/* Always enable the fan */
	fan_set_enabled(1);

	/* Disable thermal engine automatic fan control. */
	fan_set_thermal_control_enabled(0);

	/* Set the duty cycle */
	fan_set_duty_raw(pwm);
}

/*****************************************************************************/
/* Console commands */

static int cc_fanauto(int argc, char **argv)
{
	fan_set_thermal_control_enabled(1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanauto, cc_fanauto,
			NULL,
			"Enable thermal fan control",
			NULL);


static int cc_faninfo(int argc, char **argv)
{
	int tmp;
	ccprintf("Actual: %4d rpm\n", fan_get_rpm_actual());
	ccprintf("Target: %4d rpm\n", fan_get_rpm_target());
	ccprintf("Duty:   %d%%\n", fan_get_duty_cycle());
	tmp = fan_get_status();
	ccprintf("Status: %d (%s)\n", tmp, human_status[tmp]);
	ccprintf("Mode:   %s\n", fan_get_rpm_mode() ? "rpm" : "duty");
	ccprintf("Auto:   %s\n", thermal_control_enabled ? "yes" : "no");
	ccprintf("Enable: %s\n", fan_get_enabled() ? "yes" : "no");
#ifdef CONFIG_PWM_FAN_POWER_GOOD
	ccprintf("Power:  %s\n",
#ifdef CONFIG_PWM_FAN_EN_GPIO
		 gpio_get_level(CONFIG_PWM_FAN_EN_GPIO) &&
#endif
		 gpio_get_level(CONFIG_PWM_FAN_POWER_GOOD) ? "yes" : "no");
#endif


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
		rpm = pwm_fan_percent_to_rpm(rpm);
	} else if (*e) {
		return EC_ERROR_PARAM1;
	}

	/* Move the fan to automatic control */
	fan_set_rpm_mode(1);

	/* Always enable the fan */
	fan_set_enabled(1);

	/* Disable thermal engine automatic fan control. */
	fan_set_thermal_control_enabled(0);

	fan_set_rpm_target(rpm);

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
	fan_set_duty_cycle(percent);

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

	r->rpm = fan_get_rpm_target();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_FAN_TARGET_RPM,
		     hc_pwm_get_fan_target_rpm,
		     EC_VER_MASK(0));

static int hc_pwm_set_fan_target_rpm(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_target_rpm *p = args->params;

	fan_set_thermal_control_enabled(0);
	fan_set_rpm_mode(1);
	fan_set_rpm_target(p->rpm);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_TARGET_RPM,
		     hc_pwm_set_fan_target_rpm,
		     EC_VER_MASK(0));

static int hc_pwm_set_fan_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_duty *p = args->params;
	fan_set_duty_cycle(p->percent);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_DUTY,
		     hc_pwm_set_fan_duty,
		     EC_VER_MASK(0));

static int hc_thermal_auto_fan_ctrl(struct host_cmd_handler_args *args)
{
	fan_set_thermal_control_enabled(1);
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

	/* Enable the fan module and delay a few clocks */
	LM4_SYSTEM_RCGCFAN = 1;
	clock_wait_cycles(3);

	/* Configure GPIOs */
	gpio_config_module(MODULE_PWM_FAN, 1);

	/* Disable all fans */
	LM4_FAN_FANCTL = 0;

	/*
	 * Configure CPU fan:
	 * 0x8000 = bit 15     = auto-restart
	 * 0x0000 = bit 14     = slow acceleration
	 * 0x0000 = bits 13:11 = no hysteresis
	 * 0x0000 = bits 10:8  = start period (2<<0) edges
	 * 0x0000 = bits 7:6   = no fast start
	 * 0x0020 = bits 5:4   = average 4 edges when calculating RPM
	 * 0x000c = bits 3:2   = 8 pulses per revolution
	 *                       (see note at top of file)
	 * 0x0000 = bit 0      = automatic control
	 */
	LM4_FAN_FANCH(FAN_CH_CPU) = 0x802c;

	prev = (const struct pwm_fan_state *)
		system_get_jump_tag(PWMFAN_SYSJUMP_TAG, &version, &size);
	if (prev && version == PWM_HOOK_VERSION && size == sizeof(*prev)) {
		/* Restore previous state. */
		fan_set_enabled(prev->fan_en);
		fan_set_rpm_target(prev->fan_rpm);
	} else {
		/* Set initial fan speed to maximum */
		pwm_fan_set_percent_needed(100);
	}

	fan_set_thermal_control_enabled(1);

	/* Initialize memory-mapped data */
	mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN);
	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++)
		mapped[i] = EC_FAN_SPEED_NOT_PRESENT;
}
DECLARE_HOOK(HOOK_INIT, pwm_fan_init, HOOK_PRIO_DEFAULT);

static void pwm_fan_second(void)
{
	uint16_t *mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN);

	if (fan_is_stalled()) {
		mapped[0] = EC_FAN_SPEED_STALLED;
		/*
		 * Issue warning.  As we have thermal shutdown
		 * protection, issuing warning here should be enough.
		 */
		host_set_single_event(EC_HOST_EVENT_THERMAL);
		cprintf(CC_PWM, "[%T Fan stalled!]\n");
	} else {
		mapped[0] = fan_get_rpm_actual();
	}
}
DECLARE_HOOK(HOOK_SECOND, pwm_fan_second, HOOK_PRIO_DEFAULT);

static void pwm_fan_preserve_state(void)
{
	struct pwm_fan_state state;

	state.fan_en = fan_get_enabled();
	state.fan_rpm = fan_get_rpm_target();

	system_add_jump_tag(PWMFAN_SYSJUMP_TAG, PWM_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, pwm_fan_preserve_state, HOOK_PRIO_DEFAULT);

static void pwm_fan_resume(void)
{
	fan_set_enabled(1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pwm_fan_resume, HOOK_PRIO_DEFAULT);

static void pwm_fan_S3_S5(void)
{
	/* Take back fan control when the processor shuts down */
	fan_set_thermal_control_enabled(1);
	/* For now don't do anything with it. We'll have to turn it on again if
	 * we need active cooling during heavy battery charging or something.
	 */
	fan_set_rpm_target(0);
	fan_set_enabled(0);			/* crosbug.com/p/8097 */

}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pwm_fan_S3_S5, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pwm_fan_S3_S5, HOOK_PRIO_DEFAULT);
