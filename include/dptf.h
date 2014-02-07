/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions used to provide the Intel DPTF interface over ACPI */

#ifndef __CROS_EC_DPTF_H
#define __CROS_EC_DPTF_H

/**
 * Set fan duty target.
 *
 * 0-100% sets fixed duty cycle, out of range means let the EC drive.
 */
void dptf_set_fan_duty_target(int pct);

/**
 * Return 0-100% if in duty mode. -1 if not.
 */
int dptf_get_fan_duty_target(void);

/* Thermal thresholds may be set for each temp sensor. */
#define DPTF_THRESHOLDS_PER_SENSOR 2
#define DPTF_THRESHOLD_HYSTERESIS 2

/**
 * Set/enable the thresholds.
 */
void dptf_set_temp_threshold(int sensor_id,	/* zero-based sensor index */
			     int temp,		/* in degrees K */
			     int idx,		/* which threshold (0 or 1) */
			     int enable);	/* true = on, false = off */

/**
 * Return the ID of a temp sensor that has crossed its threshold since the last
 * time we asked. -1 means none.
 */
int dptf_query_next_sensor_event(void);

/**
 * Set charging current limit, in mA.  -1 means no limit.
 */
void dptf_set_charging_current_limit(int ma);

/**
 * Get charging current limit, in mA, or -1 if not DPTF-limiting.
 */
int dptf_get_charging_current_limit(void);

#endif	/* __CROS_EC_DPTF_H */
