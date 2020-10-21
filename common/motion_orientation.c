/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement an orientation sensor. */

#include "motion_orientation.h"

/*
 * Orientation mode vectors, must match sequential ordering of
 * known orientations from enum motionsensor_orientation
 */
static const intv3_t orientation_modes[] = {
	[MOTIONSENSE_ORIENTATION_LANDSCAPE] = { 0, -1, 0 },
	[MOTIONSENSE_ORIENTATION_PORTRAIT] = { 1, 0, 0 },
	[MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_PORTRAIT] = { -1, 0, 0 },
	[MOTIONSENSE_ORIENTATION_UPSIDE_DOWN_LANDSCAPE] = { 0, 1, 0 },
};

enum motionsensor_orientation motion_orientation_remap(
		const struct motion_sensor_t *s,
		enum motionsensor_orientation orientation)
{
	enum motionsensor_orientation rotated_orientation;
	const intv3_t *orientation_v;
	intv3_t rotated_orientation_v;

	if (orientation == MOTIONSENSE_ORIENTATION_UNKNOWN)
		return MOTIONSENSE_ORIENTATION_UNKNOWN;

	orientation_v = &orientation_modes[orientation];
	rotate(*orientation_v, *s->rot_standard_ref, rotated_orientation_v);
	rotated_orientation = ((2 * rotated_orientation_v[1] +
			rotated_orientation_v[0] + 4) % 5);
	return rotated_orientation;
}
