/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Nvidia GPU D-Notify driver
 */

#include <stddef.h>

#include "charge_manager.h"
#include "charge_state.h"
#include "compile_time_macros.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "nvidia_gpu.h"
#include "throttle_ap.h"
#include "timer.h"

#define CPRINTS(fmt, args...) cprints(CC_GPU, "GPU: " fmt, ## args)
#define CPRINTF(fmt, args...) cprintf(CC_GPU, "GPU: " fmt, ## args)

/*
 * BIT0~2: D-Notify level (0:D1, ... 4:D5)
 * note: may need a bit for disabling dynamic boost.
 */
#define MEMMAP_D_NOTIFY_MASK		GENMASK(2, 0)

test_export_static enum d_notify_level d_notify_level = D_NOTIFY_1;
test_export_static bool policy_initialized = false;
test_export_static const struct d_notify_policy *d_notify_policy = NULL;

void nvidia_gpu_init_policy(const struct d_notify_policy *policy)
{
	if (policy) {
		d_notify_policy = policy;
		policy_initialized = true;
	}
}

static void set_d_notify_level(enum d_notify_level level)
{
	uint8_t *memmap_gpu = (uint8_t *)host_get_memmap(EC_MEMMAP_GPU);

	if (level == d_notify_level)
		return;

	d_notify_level = level;
	*memmap_gpu = (*memmap_gpu & ~MEMMAP_D_NOTIFY_MASK) | d_notify_level;
	host_set_single_event(EC_HOST_EVENT_GPU);
	CPRINTS("Set D-notify level to D%c", ('1' + (int)d_notify_level));
}

static void evaluate_d_notify_level(void)
{
	enum d_notify_level lvl;
	const struct d_notify_policy *policy = d_notify_policy;

	/*
	 * We don't need to care about 'transitioning to S0' because throttling
	 * is unlikely required when the system is about to start.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	if (!policy_initialized) {
		CPRINTS("WARN: %s called before policies are set.", __func__);
		return;
	}

	if (extpower_is_present()) {
		const int watts = charge_manager_get_power_limit_uw() / 1000000;

		for (lvl = D_NOTIFY_1; lvl <= D_NOTIFY_5; lvl++) {
			if (policy[lvl].power_source != D_NOTIFY_AC &&
			    policy[lvl].power_source != D_NOTIFY_AC_DC)
				continue;

			if (policy[lvl].power_source == D_NOTIFY_AC) {
				if (watts >= policy[lvl].ac.min_charger_watts) {
					set_d_notify_level(lvl);
					break;
				}
			} else {
				set_d_notify_level(lvl);
				break;
			}
		}
	} else {
		const int soc = charge_get_percent();

		for (lvl = D_NOTIFY_5; lvl >= D_NOTIFY_1; lvl--) {
			if (policy[lvl].power_source == D_NOTIFY_DC) {
				if (soc <= policy[lvl].dc.min_battery_soc) {
					set_d_notify_level(lvl);
					break;
				}
			} else if (policy[lvl].power_source == D_NOTIFY_AC_DC) {
				set_d_notify_level(lvl);
				break;
			}
		}
	}
}

static void disable_gpu_acoff(void)
{
	gpio_set_level(GPIO_NVIDIA_GPU_ACOFF_ODL, 1);
	evaluate_d_notify_level();
}
DECLARE_DEFERRED(disable_gpu_acoff);

static void handle_battery_soc_change(void)
{
	evaluate_d_notify_level();
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, handle_battery_soc_change,
	     HOOK_PRIO_DEFAULT);

/*
 * This function enables and disables both hard and soft throttles. (Thus,
 * <type> has no meaning.).
 *
 * When throttling, it hard-throttles the GPU and sets the D-level to D5. It
 * also schedules a deferred call to disable the hard throttle. So, it's not
 * necessary to call it for unthrottling.
 *
 * Currently, it's upto each board when this is called. For example, it can be
 * called from board_set_active_charge_port since board_set_active_charge_port
 * is called whenever (and prior to) active port or active supplier or both
 * changes.
 */
void throttle_gpu(enum throttle_level level,
		  enum throttle_type type, /* not used */
		  enum throttle_sources source)
{
	if (level == THROTTLE_ON) {
		/* Cancel pending deferred call. */
		hook_call_deferred(&disable_gpu_acoff_data, -1);
		/* Toggle hardware throttle immediately. */
		gpio_set_level(GPIO_NVIDIA_GPU_ACOFF_ODL, 0);
		/*
		 * Switch to the lowest (D5) first then move up as the situation
		 * improves.
		 */
		set_d_notify_level(D_NOTIFY_5);
		hook_call_deferred(&disable_gpu_acoff_data,
				   NVIDIA_GPU_ACOFF_DURATION);
	} else {
		disable_gpu_acoff();
	}
}
