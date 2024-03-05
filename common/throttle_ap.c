/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common chipset throttling code for Chrome EC */

#include "builtin/assert.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "throttle_ap.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)

/*
 * When C10 deasserts, PROCHOT may also change state when the corresponding
 * power rail is turned back on. Recheck PROCHOT directly from the C10 exit
 * using a shorter debounce than the PROCHOT interrupt.
 */
#define C10_IN_DEBOUNCE_US (10 * MSEC)

/*****************************************************************************/
/* This enforces the virtual OR of all throttling sources. */
static K_MUTEX_DEFINE(throttle_mutex);
static uint32_t throttle_request[NUM_THROTTLE_TYPES];
static int debounced_prochot_in;
static const struct prochot_cfg *prochot_cfg;

void throttle_ap(enum throttle_level level, enum throttle_type type,
		 enum throttle_sources source)
{
	uint32_t tmpval, bitmask;

	mutex_lock(&throttle_mutex);

	bitmask = BIT(source);

	switch (level) {
	case THROTTLE_ON:
		throttle_request[type] |= bitmask;
		break;
	case THROTTLE_OFF:
		throttle_request[type] &= ~bitmask;
		break;
	}

	tmpval = throttle_request[type]; /* save for printing */

	switch (type) {
	case THROTTLE_SOFT:
#ifdef HAS_TASK_HOSTCMD
		host_throttle_cpu(tmpval);
#endif
		break;
	case THROTTLE_HARD:
#ifdef CONFIG_CHIPSET_CAN_THROTTLE
		chipset_throttle_cpu(tmpval);
#endif
		break;

	case NUM_THROTTLE_TYPES:
		/* Make the compiler shut up. Don't use 'default', because
		 * we still want to catch any new types.
		 */
		break;
	}

	mutex_unlock(&throttle_mutex);

	/* print outside the mutex */
	CPRINTS("set AP throttling type %d to %s (0x%08x)", type,
		tmpval ? "on" : "off", tmpval);
}

void throttle_ap_config_prochot(const struct prochot_cfg *cfg)
{
	prochot_cfg = cfg;

	if (IS_ENABLED(CONFIG_THROTTLE_AP_SINGLE_PIN)) {
		gpio_set_flags(prochot_cfg->gpio_prochot_in, GPIO_INPUT);
	}
}

__maybe_unused static bool prochot_is_gated_by_c10(int prochot_in)
{
#ifdef CONFIG_CPU_PROCHOT_GATE_ON_C10
	int c10_in = gpio_get_level(prochot_cfg->gpio_c10_in);

	if (!prochot_cfg->c10_active_high)
		c10_in = !c10_in;

	if (c10_in && prochot_in) {
		return true;
	}
#endif
	return false;
}

static void prochot_input_deferred(void)
{
	int prochot_in;

	/*
	 * Validate board called throttle_ap_config_prochot().
	 */
	ASSERT(prochot_cfg);

	prochot_in = gpio_get_level(prochot_cfg->gpio_prochot_in);

	if (IS_ENABLED(CONFIG_CPU_PROCHOT_ACTIVE_LOW))
		prochot_in = !prochot_in;

	if (prochot_in == debounced_prochot_in)
		return;

	/*
	 * b/173180788 Confirmed from Intel internal that SLP_S3# asserts low
	 * about 10us before PROCHOT# asserts low, which means that
	 * the CPU is already in reset and therefore the PROCHOT#
	 * asserting low is normal behavior and not a concern
	 * for PROCHOT# event.  Ignore all PROCHOT changes while the AP is off
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF | CHIPSET_STATE_ANY_SUSPEND))
		return;

	/*
	 * b/185810479 When the AP enters C10, the PROCHOT signal may not be
	 * valid. Refer to the CONFIG_CPU_PROCHOT_GATE_ON_C10 documentation
	 * for details.
	 */
	if (prochot_is_gated_by_c10(prochot_in))
		return;

	debounced_prochot_in = prochot_in;

	if (debounced_prochot_in) {
		CPRINTS("External PROCHOT assertion detected");
#if defined(CONFIG_FANS) && !defined(CONFIG_THROTTLE_AP_NO_FAN)
		dptf_set_fan_duty_target(100);
#endif
	} else {
		CPRINTS("External PROCHOT condition cleared");
#if defined(CONFIG_FANS) && !defined(CONFIG_THROTTLE_AP_NO_FAN)
		/* Revert to automatic control of the fan */
		dptf_set_fan_duty_target(-1);
#endif
	}

	if (prochot_cfg->callback)
		prochot_cfg->callback(debounced_prochot_in,
				      prochot_cfg->callback_data);
}
DECLARE_DEFERRED(prochot_input_deferred);

void throttle_ap_prochot_input_interrupt(enum gpio_signal signal)
{
	/*
	 * Trigger deferred notification of PROCHOT change so we can ignore
	 * any pulses that are too short.
	 */
	hook_call_deferred(&prochot_input_deferred_data,
			   PROCHOT_IN_DEBOUNCE_US);
}

#ifdef CONFIG_CPU_PROCHOT_GATE_ON_C10
void throttle_ap_c10_input_interrupt(enum gpio_signal signal)
{
	/*
	 * This interrupt is configured to fire only when the AP exits C10
	 * and de-asserts the C10 signal. Recheck the PROCHOT signal in case
	 * another PROCHOT source is active when the AP exits C10.
	 */
	hook_call_deferred(&prochot_input_deferred_data, C10_IN_DEBOUNCE_US);
}
#endif

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_APTHROTTLE
static int command_apthrottle(int argc, const char **argv)
{
	int i;
	uint32_t tmpval;

	for (i = 0; i < NUM_THROTTLE_TYPES; i++) {
		mutex_lock(&throttle_mutex);
		tmpval = throttle_request[i];
		mutex_unlock(&throttle_mutex);

		ccprintf("AP throttling type %d is %s (0x%08x)\n", i,
			 tmpval ? "on" : "off", tmpval);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(apthrottle, command_apthrottle, NULL,
			"Display the AP throttling state");
#endif
