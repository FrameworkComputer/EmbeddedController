/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test thermal engine.
 */

#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "test_util.h"
#include "thermal.h"
#include "timer.h"
#include "util.h"

#define FAN_RPM(fan) fans[fan].rpm

/*****************************************************************************/
/* Tests */

void set_thermal_control_enabled(int fan, int enable);

static int test_fan(void)
{
	/* "actual" fan speed from board/host/fan.c */
	extern int mock_rpm;

	sleep(2);

	/* Fans initialize disabled. */
	TEST_ASSERT(fan_get_rpm_actual(0) == 0);

	set_thermal_control_enabled(0, 1);

	/*
	 * fan_set_percent_needed() is normally called once a second by the
	 * thermal task, but we're not using a thermal test in this test so
	 * we can dink around with the fans without having to wait. The host
	 * implementation just sets mock_rpm to whatever it's asked for.
	 */

	/* Off */
	fan_set_percent_needed(0, 0);
	TEST_ASSERT(fan_get_rpm_actual(0) == 0);
	fan_set_percent_needed(0, 0);
	TEST_ASSERT(fan_get_rpm_actual(0) == 0);

	/* On, but just barely */
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_start);
	/* fan is above min speed now, so should be set to min */
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_min);

	/* Full speed */
	fan_set_percent_needed(0, 100);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_max);
	fan_set_percent_needed(0, 100);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_max);

	/* Slow again */
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_min);
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_min);

	/* Off */
	fan_set_percent_needed(0, 0);
	TEST_ASSERT(fan_get_rpm_actual(0) == 0);
	fan_set_percent_needed(0, 0);
	TEST_ASSERT(fan_get_rpm_actual(0) == 0);

	/* On, but just barely */
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_start);
	/* Force the mock_rpm to be slow, to simulate dragging */
	mock_rpm = FAN_RPM(0)->rpm_min - 105;
	/* It should keep trying for the start speed */
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_start);
	/* But we have to keep forcing the mock_rpm back down */
	mock_rpm = FAN_RPM(0)->rpm_min - 105;
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_start);
	/* Now let it turn just under rpm_min. Should be okay there. */
	mock_rpm = FAN_RPM(0)->rpm_min - 10;
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_min);
	/* Let it go a little faster, still okay */
	mock_rpm = FAN_RPM(0)->rpm_min + 10;
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_min);
	/* But if it drops too low, it should go back to the start speed */
	mock_rpm = FAN_RPM(0)->rpm_min - 105;
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_start);
	/* And then relax */
	fan_set_percent_needed(0, 1);
	TEST_ASSERT(fan_get_rpm_actual(0) == FAN_RPM(0)->rpm_min);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fan);

	test_print_result();
}

/* Doesn't do anything, but it makes this test stop intermittently covering
 * some code in core/host/task.c:fast_forward().
 */
void interrupt_generator(void)
{
}
