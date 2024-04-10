/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test clamshell/tablet when the sensors are broken.
 */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "motion_common.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

/*****************************************************************************/
/* Mock functions */
static int accel_init(struct motion_sensor_t *s)
{
	return EC_ERROR_UNKNOWN;
}

/* Only populate _init(), sensor stack should not touch the sensors on failure.
 */
const struct accelgyro_drv test_motion_sense = {
	.init = accel_init,
};

struct motion_sensor_t motion_sensors[] = {
	[BASE] = {
		.name = "base",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_LSM6DS0,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &test_motion_sense,
		.rot_standard_ref = NULL,
		.default_range = 2,
	},
	[LID] = {
		.name = "lid",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_KXCJ9,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &test_motion_sense,
		.rot_standard_ref = NULL,
		.default_range = 2,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static int tablet_hook_count;
/* Store value obtained by caller. */
static int tablet_hook_value;

static void tablet_mode_change_hook(void)
{
	tablet_hook_count++;
	tablet_hook_value = tablet_get_mode();
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, tablet_mode_change_hook,
	     HOOK_PRIO_DEFAULT);

void before_test(void)
{
	/* Make sure the device lid is in a consistent state (close). */
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	crec_msleep(50);
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(50);
	tablet_hook_count = 1;
}

/*
 * The device is in clamshell mode from before_test(),
 * Go through GPIO transitions and observe the table mode state.
 */
test_static int test_start_lid_close(void)
{
	TEST_ASSERT(!tablet_get_mode());

	/* Opening, No change. */
	gpio_set_level(GPIO_LID_OPEN, 1);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 1);
	TEST_ASSERT(!tablet_get_mode());

	/* full 360, tablet mode. */
	gpio_set_level(GPIO_TABLET_MODE_L, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/* Going out of 360 mode, no change. */
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/* Back to close. */
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 3);
	TEST_ASSERT(!tablet_get_mode());

	return EC_SUCCESS;
}

/*
 * Put the device in tablet mode first.
 * Reset the EC, keep the existing GPIO level.
 * Verify the state is not forgotten when the EC starts in tablet mode after
 * reset.
 */
test_static int test_start_tablet_mode(void)
{
	/* Go in tablet mode */
	gpio_set_level(GPIO_LID_OPEN, 1);
	gpio_set_level(GPIO_TABLET_MODE_L, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 2);

	/* Shutdown device */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);

	crec_msleep(50);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S5);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	return EC_SUCCESS;
}

/*
 * Put the device in tablet mode first.
 * Do a fast transition from 0 degree to 360 degree:
 * Observe the transition happens.
 * then 360 degree to 0 degree.
 * Observe the transition happens.
 */
test_static int test_fast_transition(void)
{
	TEST_ASSERT(!tablet_get_mode());

	/* Go in tablet mode fast.*/
	gpio_set_level(GPIO_LID_OPEN, 1);
	gpio_set_level(GPIO_TABLET_MODE_L, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_get_mode());
	TEST_ASSERT(tablet_hook_count == 2);

	/* Go in clamshell mode fast.*/
	gpio_set_level(GPIO_LID_OPEN, 0);
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	crec_msleep(50);
	TEST_ASSERT(!tablet_get_mode());
	TEST_ASSERT(tablet_hook_count == 3);

	return EC_SUCCESS;
}

/*
 * Simulate disabling the sensors from boad_init() - when
 * the firmware can work for both clamsheel and convertible.
 */
test_static int test_disable_sensors_GMR_low(void)
{
	/* Assume lid is open */
	gpio_set_level(GPIO_LID_OPEN, 1);
	/* GMR is not stuffed, assume low. */
	gpio_set_level(GPIO_TABLET_MODE_L, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());
	TEST_ASSERT(tablet_hook_value == tablet_get_mode());

	/* Disable tablet mode deteciton.*/
	gmr_tablet_switch_disable();
	motion_sensor_count = 0;

	/* We should not be in tablet mode. */
	TEST_ASSERT(!tablet_get_mode());
	TEST_ASSERT(tablet_hook_value == tablet_get_mode());

	return EC_SUCCESS;
}

/*
 * Simulate disabling the sensors from boad_init() - when
 * the firmware can work for both clamsheel and convertible.
 */
test_static int test_disable_sensors_GMR_high(void)
{
	/* Assume lid is open */
	gpio_set_level(GPIO_LID_OPEN, 1);
	/* GMR is not stuffed, assume high. */
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 1);
	TEST_ASSERT(!tablet_get_mode());
	TEST_ASSERT(tablet_hook_value == tablet_get_mode());

	/* Disable tablet mode deteciton.*/
	gmr_tablet_switch_disable();
	motion_sensor_count = 0;

	/* We should not be in tablet mode. */
	TEST_ASSERT(!tablet_get_mode());
	TEST_ASSERT(tablet_hook_value == tablet_get_mode());

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_start_lid_close);
	RUN_TEST(test_start_tablet_mode);
	RUN_TEST(test_fast_transition);

	RUN_TEST(test_disable_sensors_GMR_low);
	RUN_TEST(test_disable_sensors_GMR_high);

	test_print_result();
}
