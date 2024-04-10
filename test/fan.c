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

	crec_sleep(2);

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

/* Provide a test driver to make test easier to read. */
int temp_to_rpm(int temperature_c)
{
	const int temp_fan_off = C_TO_K(35);
	const int temp_fan_max = C_TO_K(55);
	const struct fan_step_1_1 fan_table[] = {
		{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(35),
		  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(41),
		  .rpm = 2500 },
		{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(37),
		  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(43),
		  .rpm = 3200 },
		{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(42),
		  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(45),
		  .rpm = 3500 },
		{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(44),
		  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(47),
		  .rpm = 3900 },
		{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(46),
		  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(49),
		  .rpm = 4500 },
		{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(48),
		  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(52),
		  .rpm = 5100 },
		{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(51),
		  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(55),
		  .rpm = 5400 },
	};
	const int num_fan_levels = ARRAY_SIZE(fan_table);
	int temp_ratio = TEMP_TO_RATIO(temperature_c);

	int rpm = temp_ratio_to_rpm_hysteresis(fan_table, num_fan_levels, 0,
					       temp_ratio, NULL);

	fan_set_rpm_target(FAN_CH(0), rpm);
	return rpm;
}

static int test_temp_ratio_to_rpm_hysteresis(void)
{
	const int ZERO = 0;
	/* set initial value to be different so that a log message appears */
	fan_set_rpm_target(FAN_CH(0), 5400);
	/* initial turn-on behavior, ramp up. @ represents fan speed; + temp */
	TEST_ASSERT(temp_to_rpm(30) == ZERO); /* @+. .   40    .   50    .60 */
	TEST_ASSERT(temp_to_rpm(30) == ZERO); /* @+. .    .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(35) == ZERO); /* @   +    .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(37) == ZERO); /* @   . +  .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(39) == ZERO); /* @   .   +.    .    .    .   */
	TEST_ASSERT(temp_to_rpm(40) == ZERO); /* @   .    +    .    .    .   */
	TEST_ASSERT(temp_to_rpm(41) == 2500); /*    @.    .+   .    .    .   */
	TEST_ASSERT(temp_to_rpm(36) == 2500); /*    @.+   .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(42) == 2500); /*    @.    . +  .    .    .   */
	TEST_ASSERT(temp_to_rpm(43) == 3200); /*     @    .  + .    .    .   */
	TEST_ASSERT(temp_to_rpm(38) == 3200); /*     @  + .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(44) == 3200); /*     @    .   +.    .    .   */
	TEST_ASSERT(temp_to_rpm(45) == 3500); /*     .@   .    +    .    .   */
	TEST_ASSERT(temp_to_rpm(43) == 3500); /*     .@   .  + .    .    .   */
	TEST_ASSERT(temp_to_rpm(46) == 3500); /*     .@   .    .+   .    .   */
	TEST_ASSERT(temp_to_rpm(47) == 3900); /*     . @  .    . +  .    .   */
	TEST_ASSERT(temp_to_rpm(45) == 3900); /*     . @  .    +    .    .   */
	TEST_ASSERT(temp_to_rpm(48) == 3900); /*     . @  .    .  + .    .   */
	TEST_ASSERT(temp_to_rpm(49) == 4500); /*     .  @ .    .   +.    .   */
	TEST_ASSERT(temp_to_rpm(47) == 4500); /*     .  @ .    . +  .    .   */
	TEST_ASSERT(temp_to_rpm(51) == 4500); /*     .  @ .    .    .+   .   */
	TEST_ASSERT(temp_to_rpm(52) == 5100); /*     .   @.    .    . +  .   */
	TEST_ASSERT(temp_to_rpm(49) == 5100); /*     .   @.    .   +.    .   */
	TEST_ASSERT(temp_to_rpm(54) == 5100); /*     .   @.    .    .   +.   */
	TEST_ASSERT(temp_to_rpm(55) == 5400); /*     .    @    .    .    +   */
	TEST_ASSERT(temp_to_rpm(52) == 5400); /*     .    @    .    . +  .   */
	TEST_ASSERT(temp_to_rpm(60) == 5400); /*     .    @    .   50    ..+ */
	/* cool-down */
	TEST_ASSERT(temp_to_rpm(55) == 5400); /*     .    @    .    .    +   */
	TEST_ASSERT(temp_to_rpm(52) == 5400); /*     .    @    .    . +  .   */
	TEST_ASSERT(temp_to_rpm(51) == 5100); /*     .   @.    .    .+   .   */
	TEST_ASSERT(temp_to_rpm(54) == 5100); /*     .   @.    .    .   +.   */
	TEST_ASSERT(temp_to_rpm(49) == 5100); /*     .   @.    .   +.    .   */
	TEST_ASSERT(temp_to_rpm(48) == 4500); /*     .  @ .    .  + .    .   */
	TEST_ASSERT(temp_to_rpm(51) == 4500); /*     .  @ .    .    .+   .   */
	TEST_ASSERT(temp_to_rpm(47) == 4500); /*     .  @ .    . +  .    .   */
	TEST_ASSERT(temp_to_rpm(46) == 3900); /*     . @  .    .+   .    .   */
	TEST_ASSERT(temp_to_rpm(48) == 3900); /*     . @  .    .  + .    .   */
	TEST_ASSERT(temp_to_rpm(45) == 3900); /*     . @  .    +    .    .   */
	TEST_ASSERT(temp_to_rpm(44) == 3500); /*     .@   .   +.    .    .   */
	TEST_ASSERT(temp_to_rpm(46) == 3500); /*     .@   .    .+   .    .   */
	TEST_ASSERT(temp_to_rpm(43) == 3500); /*     .@   .  + .    .    .   */
	TEST_ASSERT(temp_to_rpm(42) == 3200); /*     @    . +  .    .    .   */
	TEST_ASSERT(temp_to_rpm(44) == 3200); /*     @    .   +.    .    .   */
	TEST_ASSERT(temp_to_rpm(38) == 3200); /*     @  + .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(37) == 2500); /*    @. +  .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(42) == 2500); /*    @.    . +  .    .    .   */
	TEST_ASSERT(temp_to_rpm(36) == 2500); /*    @.+   .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(35) == ZERO); /* @   +   40    .   50    .   */
	/* warm up again */
	TEST_ASSERT(temp_to_rpm(38) == ZERO); /* @   .  + .    .    .    .   */
	/* jumping */
	TEST_ASSERT(temp_to_rpm(46) == 3500); /*     .@   .    .+   .    .   */
	TEST_ASSERT(temp_to_rpm(36) == 2500); /*    @.+   .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(35) == ZERO); /* @   +    .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(37) == ZERO); /* @   . +  .    .    .    .   */
	TEST_ASSERT(temp_to_rpm(46) == 3500); /*     .@   .    .+   .    .   */
	TEST_ASSERT(temp_to_rpm(54) == 5100); /*     .   @.    .    .   +.   */
	TEST_ASSERT(temp_to_rpm(55) == 5400); /*     .    @    .    .    +   */
	TEST_ASSERT(temp_to_rpm(60) == 5400); /*     .    @    .    .    ..+ */
	TEST_ASSERT(temp_to_rpm(53) == 5400); /*     .    @    .    .  + .   */
	TEST_ASSERT(temp_to_rpm(46) == 3900); /*     . @  .    .+   .    .   */
	TEST_ASSERT(temp_to_rpm(30) == ZERO); /* @+. .   40    .   50    .   */

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fan);
	RUN_TEST(test_temp_ratio_to_rpm_hysteresis);

	test_print_result();
}

/* Doesn't do anything, but it makes this test stop intermittently covering
 * some code in core/host/task.c:fast_forward().
 */
void interrupt_generator(void)
{
}
