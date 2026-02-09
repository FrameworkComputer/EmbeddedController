/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "common.h"
#include "cros_cbi.h"
#include "driver/accel_lis2dh.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"

void motion_interrupt(enum gpio_signal signal)
{
}

static void alt_sensor_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
