/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions used to provide the Intel DPTF interface over ACPI */

#ifndef __CROS_EC_DPTF_H
#define __CROS_EC_DPTF_H

/* 0-100% sets fixed duty cycle, out of range means let the EC drive */
void dptf_set_fan_duty_target(int pct);

/* 0-100% if in duty mode. -1 if not */
int dptf_get_fan_duty_target(void);

/* Each temp sensor can have up to two thresholds */
void dptf_set_temp_threshold(int sensor_id,	/* zero-based sensor index */
			     int temp,		/* in degrees K */
			     int idx,		/* which threshold (0 or 1) */
			     int enable);	/* true = on, false = off */

#endif	/* __CROS_EC_DPTF_H */
