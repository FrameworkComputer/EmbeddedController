/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fan control module for Chrome EC */

#ifndef __CROS_EC_FAN_H
#define __CROS_EC_FAN_H

/**
 * Set the amount of active cooling needed. The thermal control task will call
 * this frequently, and the fan control logic will attempt to provide it.
 *
 * @param pct   Percentage of cooling effort needed (0 - 100)
 */
void pwm_fan_set_percent_needed(int pct);

/**
 * This function translates the percentage of cooling needed into a target RPM.
 * The default implementation should be sufficient for most needs, but
 * individual boards may provide a custom version if needed (see config.h).
 *
 * @param pct   Percentage of cooling effort needed (always in [0,100])
 * Return       Target RPM for fan
 */
int pwm_fan_percent_to_rpm(int pct);

#endif  /* __CROS_EC_FAN_H */
