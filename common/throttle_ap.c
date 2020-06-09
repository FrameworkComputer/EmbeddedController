/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common chipset throttling code for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "throttle_ap.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

#define PROCHOT_IN_DEBOUNCE_US		(100 * MSEC)

/*****************************************************************************/
/* This enforces the virtual OR of all throttling sources. */
static struct mutex throttle_mutex;
static uint32_t throttle_request[NUM_THROTTLE_TYPES];
static int debounced_prochot_in;
static enum gpio_signal gpio_prochot_in = GPIO_COUNT;

void throttle_ap(enum throttle_level level,
		 enum throttle_type type,
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

	tmpval = throttle_request[type];	/* save for printing */

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
	CPRINTS("set AP throttling type %d to %s (0x%08x)",
		type, tmpval ? "on" : "off", tmpval);

}

static void prochot_input_deferred(void)
{
	int prochot_in;

	/*
	 * Shouldn't be possible, but better to protect against buffer
	 * overflow
	 */
	ASSERT(signal_is_gpio(gpio_prochot_in));

	prochot_in = gpio_get_level(gpio_prochot_in);

	if (IS_ENABLED(CONFIG_CPU_PROCHOT_ACTIVE_LOW))
		prochot_in = !prochot_in;

	if (prochot_in == debounced_prochot_in)
		return;

	debounced_prochot_in = prochot_in;

	if (debounced_prochot_in) {
		CPRINTS("External PROCHOT assertion detected");
#ifdef CONFIG_FANS
		dptf_set_fan_duty_target(100);
#endif
	} else {
		CPRINTS("External PROCHOT condition cleared");
#ifdef CONFIG_FANS
		/* Revert to automatic control of the fan */
		dptf_set_fan_duty_target(-1);
#endif
	}
}
DECLARE_DEFERRED(prochot_input_deferred);

void throttle_ap_prochot_input_interrupt(enum gpio_signal signal)
{
	/*
	 * Save the PROCHOT signal that generated the interrupt so we don't
	 * rely on a specific pin name.
	 */
	if (gpio_prochot_in == GPIO_COUNT)
		gpio_prochot_in = signal;

	/*
	 * Trigger deferred notification of PROCHOT change so we can ignore
	 * any pulses that are too short.
	 */
	hook_call_deferred(&prochot_input_deferred_data,
		PROCHOT_IN_DEBOUNCE_US);
}

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_APTHROTTLE
static int command_apthrottle(int argc, char **argv)
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
DECLARE_CONSOLE_COMMAND(apthrottle, command_apthrottle,
			NULL,
			"Display the AP throttling state");
#endif
