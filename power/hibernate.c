/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_interface.h>

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

/*
 * Hibernate processing.
 *
 * When enabled, the system will be put into an extreme low
 * power state after the AP is in G3 for a configurable period of time,
 * and there is no external power connected (i.e on battery).
 *
 * The delay has a configurable default, and may be set dynamically
 * via a host command, or an EC console command. A typical delay
 * may be 1 hour (3600 seconds).
 *
 * AP events such as AP_POWER_HARD_OFF are listened for, and
 * a timer is used to detect when the AP has been off for the
 * selected delay time. If the AP is started again, the timer is canceled.
 * Once the timer expires, the system_hibernate() function is called,
 * and this will suspend the EC until a wake signal is received.
 */
static uint32_t hibernate_delay = CONFIG_HIBERNATE_DELAY_SEC;

/*
 * Return true if conditions are right for hibernation.
 */
static inline bool ready_to_hibernate(void)
{
	return ap_power_in_or_transitioning_to_state(AP_POWER_STATE_HARD_OFF) &&
	       !extpower_is_present();
}

/*
 * The AP has been off for the delay period, so hibernate the system,
 * if ready.  Called from system work queue.
 */
static void hibernate_handler(struct k_work *unused)
{
	if (ready_to_hibernate()) {
		LOG_INF("System hibernating due to %d seconds AP off",
			hibernate_delay);
		system_hibernate(0, 0);
	}
}

K_WORK_DEFINE(hibernate_work, hibernate_handler);

/*
 * Hibernate timer handler.
 * Called when timer has expired.
 * Schedule hibernate_handler to run via system work queue.
 */
static void timer_handler(struct k_timer *timer)
{
	k_work_submit(&hibernate_work);
}

K_TIMER_DEFINE(hibernate_timer, timer_handler, NULL);

/*
 * A change has been detected in either the AP state or the
 * external power supply.
 */
static void change_detected(void)
{
	if (ready_to_hibernate()) {
		/*
		 * AP is off, and there is no external power.
		 * Start the timer if it is not already running.
		 */
		if (k_timer_remaining_get(&hibernate_timer) == 0) {
			k_timer_start(&hibernate_timer,
				      K_SECONDS(hibernate_delay), K_NO_WAIT);
		}

	} else {
		/*
		 * AP is either on, or external power is on.
		 * Either way, no hibernation is done.
		 * Make sure the timer is not running.
		 */
		k_timer_stop(&hibernate_timer);
	}
}

static void ap_change(struct ap_power_ev_callback *callback,
		      struct ap_power_ev_data data)
{
	change_detected();
}

/*
 * Hook to listen for external power supply changes.
 */
DECLARE_HOOK(HOOK_AC_CHANGE, change_detected, HOOK_PRIO_DEFAULT);

/*
 * EC Console command to get/set the hibernation delay
 */
static int command_hibernation_delay(int argc, const char **argv)
{
	char *e;
	uint32_t remaining;

	if (argc >= 2) {
		uint32_t s = strtoi(argv[1], &e, 0);

		if (*e)
			return EC_ERROR_PARAM1;

		hibernate_delay = s;
	}

	/* Print the current setting */
	ccprintf("Hibernation delay: %d s\n", hibernate_delay);
	remaining = k_timer_remaining_get(&hibernate_timer);
	if (remaining == 0) {
		ccprintf("Timer not running\n");
	} else {
		ccprintf("Time remaining: %d s\n", remaining / 1000);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hibdelay, command_hibernation_delay, "[sec]",
			"Set the delay before going into hibernation");
/*
 * Host command to set the hibernation delay
 */
static enum ec_status
host_command_hibernation_delay(struct host_cmd_handler_args *args)
{
	const struct ec_params_hibernation_delay *p = args->params;
	struct ec_response_hibernation_delay *r = args->response;

	/* Only change the hibernation delay if seconds is non-zero. */
	if (p->seconds)
		hibernate_delay = p->seconds;

	r->hibernate_delay = hibernate_delay;
	/*
	 * It makes no sense to try and set these values since
	 * they are only valid when the AP is in G3 (so this
	 * host command will never be called at that point).
	 */
	r->time_g3 = 0;
	r->time_remaining = 0;

	args->response_size = sizeof(struct ec_response_hibernation_delay);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HIBERNATION_DELAY, host_command_hibernation_delay,
		     EC_VER_MASK(0));

static int hibernate_init(void)
{
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, ap_change,
				  AP_POWER_INITIALIZED | AP_POWER_HARD_OFF |
					  AP_POWER_STARTUP);
	ap_power_ev_add_callback(&cb);
	return 0;
}

SYS_INIT(hibernate_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
