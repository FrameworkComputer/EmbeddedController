/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mocked fan implementation for tests */

#include "fan.h"
#include "util.h"

const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1000,
	 .rpm_start = 1500,
	 .rpm_max = 5000,
	 .ch = 0,
	 .pgood_gpio = -1,
	 .enable_gpio = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

static int mock_enabled;
void fan_set_enabled(int ch, int enabled)
{
	mock_enabled = enabled;
}
int fan_get_enabled(int ch)
{
	return mock_enabled;
}

static int mock_percent;
void fan_set_duty(int ch, int percent)
{
	mock_percent = percent;
}
int fan_get_duty(int ch)
{
	return mock_percent;
}

static int mock_rpm_mode;
void fan_set_rpm_mode(int ch, int rpm_mode)
{
	mock_rpm_mode = rpm_mode;
}
int fan_get_rpm_mode(int ch)
{
	return mock_rpm_mode;
}

int mock_rpm;
void fan_set_rpm_target(int ch, int rpm)
{
	mock_rpm = rpm;
}
int fan_get_rpm_actual(int ch)
{
	return mock_rpm;
}
int fan_get_rpm_target(int ch)
{
	return mock_rpm;
}

enum fan_status fan_get_status(int ch)
{
	return FAN_STATUS_LOCKED;
}

int fan_is_stalled(int ch)
{
	return 0;
}

void fan_channel_setup(int ch, unsigned int flags)
{
	/* nothing to do */
}
