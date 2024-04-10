/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for Nvidia GPU.
 */
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "driver/nvidia_gpu.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "test_util.h"
#include "throttle_ap.h"
#include "timer.h"
#include "util.h"

#include <stdio.h>

struct d_notify_policy d_notify_policies[] = {
	AC_ATLEAST_W(100), AC_ATLEAST_W(65), AC_DC,
	DC_ATMOST_SOC(20), DC_ATMOST_SOC(5),
};

extern enum d_notify_level d_notify_level;
extern bool policy_initialized;
extern const struct d_notify_policy *d_notify_policy;
static int extpower_presence = 1;
static int nvidia_gpu_acoff_odl = 1;
static int charge_percent = 100;
static int charge_power = 100;
static uint8_t *memmap_gpu;

__override int charge_get_percent(void)
{
	return charge_percent;
}

__override int charge_manager_get_power_limit_uw(void)
{
	return charge_power * 1000000;
}

__override int extpower_is_present(void)
{
	return extpower_presence;
}

__override int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_NVIDIA_GPU_ACOFF_ODL)
		return nvidia_gpu_acoff_odl;
	return 0;
}

__override void gpio_set_level(enum gpio_signal signal, int value)
{
	if (signal == GPIO_NVIDIA_GPU_ACOFF_ODL)
		nvidia_gpu_acoff_odl = value;
}

static void setup(int extpower, int gpio_acoff, int percent, int power,
		  enum d_notify_level level)
{
	extpower_presence = extpower;
	nvidia_gpu_acoff_odl = gpio_acoff;
	charge_percent = percent;
	charge_power = power;
	d_notify_level = level;
	*memmap_gpu = level;
}

static void plug_ac(int plug)
{
	extpower_presence = plug;
	hook_notify(HOOK_AC_CHANGE);
}

static int check_d_notify_level(enum d_notify_level expected_level)
{
	TEST_EQ(d_notify_level, expected_level, "%d");
	TEST_EQ(*memmap_gpu, expected_level, "%d");

	return EC_SUCCESS;
}

static int test_startup(void)
{
	/* Test initial values after HOOK_INIT. Don't call setup(). */

	TEST_ASSERT(IS_ENABLED(HAS_GPU_DRIVER));
	TEST_ASSERT(policy_initialized);
	TEST_NE(d_notify_policy, NULL, "%p");
	TEST_EQ(check_d_notify_level(D_NOTIFY_1), EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

static int test_ac_unplug(void)
{
	setup(1, 1, 100, 100, D_NOTIFY_1);

	/* Unplug AC. D1 -> D5 */
	plug_ac(0);
	throttle_gpu(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_AC);
	TEST_EQ(nvidia_gpu_acoff_odl, 0, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_5), EC_SUCCESS, "%d");
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_GPU));

	/* Wait half of NVIDIA_GPU_ACOFF_DURATION. D5 -> D5. */
	crec_usleep(NVIDIA_GPU_ACOFF_DURATION / 2);
	TEST_EQ(nvidia_gpu_acoff_odl, 0, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_5), EC_SUCCESS, "%d");
	TEST_ASSERT(!host_is_event_set(EC_HOST_EVENT_GPU));

	/* Wait another half of NVIDIA_GPU_ACOFF_DURATION. D5 -> D3. */
	crec_usleep(NVIDIA_GPU_ACOFF_DURATION / 2);
	TEST_EQ(nvidia_gpu_acoff_odl, 1, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_3), EC_SUCCESS, "%d");
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_GPU));

	/* Discharge to 60%. D3 -> D3. */
	charge_percent = 60;
	hook_notify(HOOK_BATTERY_SOC_CHANGE);
	TEST_EQ(nvidia_gpu_acoff_odl, 1, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_3), EC_SUCCESS, "%d");
	TEST_ASSERT(!host_is_event_set(EC_HOST_EVENT_GPU));

	/* Discharge to 20%. D3 -> D4 */
	charge_percent = 20;
	hook_notify(HOOK_BATTERY_SOC_CHANGE);
	TEST_EQ(nvidia_gpu_acoff_odl, 1, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_4), EC_SUCCESS, "%d");
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_GPU));

	/* Discharge to 5%. D4 -> D5 */
	charge_percent = 5;
	hook_notify(HOOK_BATTERY_SOC_CHANGE);
	TEST_EQ(nvidia_gpu_acoff_odl, 1, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_5), EC_SUCCESS, "%d");
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_GPU));

	return EC_SUCCESS;
}

static int test_ac_plug(void)
{
	/* Plug 100W AC. D5 -> D1. */
	setup(0, 1, 5, 100, D_NOTIFY_5);
	plug_ac(1);
	throttle_gpu(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_AC);
	TEST_EQ(nvidia_gpu_acoff_odl, 1, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_1), EC_SUCCESS, "%d");
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_GPU));

	/* Plug 65W AC. D5 -> D2. */
	setup(0, 1, 5, 65, D_NOTIFY_5);
	plug_ac(1);
	throttle_gpu(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_AC);
	TEST_EQ(nvidia_gpu_acoff_odl, 1, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_2), EC_SUCCESS, "%d");
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_GPU));

	/* Plug 35W AC. D5 -> D3. */
	setup(0, 1, 5, 35, D_NOTIFY_5);
	plug_ac(1);
	throttle_gpu(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_AC);
	TEST_EQ(nvidia_gpu_acoff_odl, 1, "%d");
	TEST_EQ(check_d_notify_level(D_NOTIFY_3), EC_SUCCESS, "%d");
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_GPU));

	return EC_SUCCESS;
}

static int test_overt(void)
{
	nvidia_gpu_over_temp(1);
	TEST_ASSERT(*memmap_gpu & EC_MEMMAP_GPU_OVERT_BIT);
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));

	nvidia_gpu_over_temp(0);
	TEST_ASSERT(!(*memmap_gpu & EC_MEMMAP_GPU_OVERT_BIT));
	TEST_ASSERT(host_is_event_set(EC_HOST_EVENT_GPU));

	return EC_SUCCESS;
}

static void board_gpu_init(void)
{
	nvidia_gpu_init_policy(d_notify_policies);
}
DECLARE_HOOK(HOOK_INIT, board_gpu_init, HOOK_PRIO_DEFAULT);

void run_test(int argc, const char **argv)
{
	memmap_gpu = (uint8_t *)host_get_memmap(EC_MEMMAP_GPU);

	test_chipset_on();

	RUN_TEST(test_startup);
	RUN_TEST(test_ac_unplug);
	RUN_TEST(test_ac_plug);
	RUN_TEST(test_overt);
	test_print_result();
}
